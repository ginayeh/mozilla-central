"use strict";

let Cc = Components.classes;
let Ci = Components.interfaces;
let Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

const DEBUG = true;
const DB_NAME = "appcache";
const STORE_NAME = "applications";
const TOPIC_DATABASE_READY = "database-ready";
const TOPIC_DATABASE_ADD_COMPLETED = "database-add-completed";
const TOPIC_DATABASE_REMOVE_COMPLETED = "database-remove-completed";
const TOPIC_DATABASE_UPDATE_COMPLETED = "database-update-completed";
const TOPIC_APPCACHE_UPDATE = "appcache-update";
const TOPIC_APPCACHE_UPDATE_COMPLETED = "appcache-update-completed";
const TXN_READONLY = "readonly";
const TXN_READWRITE = "readwrite";
const UPDATE_FINISHED = Ci.nsIOfflineCacheUpdateObserver.STATE_FINISHED;
const UPDATE_ERROR = Ci.nsIOfflineCacheUpdateObserver.STATE_ERROR;
const UPDATE_CHECKING = Ci.nsIOfflineCacheUpdateObserver.STATE_CHECKING;
const APPLICATIONCACHE_UPDATESERVICE_CONTRACTID = "@mozilla.org/network/applicationcacheupdateservice;1";
const APPLICATIONCACHE_UPDATESERVICE_CID = Components.ID("{ac83ae97-69a0-4217-9c1f-0e3d47973b84}");
const OFFLINECACHE_UPDATESERVICE_CONTRACTID = "@mozilla.org/offlinecacheupdate-service;1";
const OBSERVERSERVICE_CONTRACTID = "@mozilla.org/observer-service;1";
const TIMER_CONTRACTID = "@mozilla.org/timer;1";

let GLOBAL_SCOPE = this;
var idbManager = Cc["@mozilla.org/dom/indexeddb/manager;1"]
                   .getService(Ci.nsIIndexedDatabaseManager);
idbManager.initWindowless(GLOBAL_SCOPE);

function ApplicationCacheUpdateService() {
  let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
                        .getService(Ci.nsIObserverService);
  observerService.addObserver(this, TOPIC_APPCACHE_UPDATE, false);
  this.init();
}

ApplicationCacheUpdateService.prototype = {
  classID: APPLICATIONCACHE_UPDATESERVICE_CID,
  contractID: APPLICATIONCACHE_UPDATESERVICE_CONTRACTID,
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIApplicationCacheUpdateService,
                                         Ci.nsIObserver]),
  db: null,

  _updateFrequency: null,

  updateTimer: null,

  updateIndex: 0,

  numEntry: 0,

  entries: [],

  /*
   * initialization
   */
  init: function init() {
    this.updateTimer = Cc[TIMER_CONTRACTID]
                       .createInstance(Ci.nsITimer);
    this.setUpdateFrequency(1/60/5);
    this.entries = new Array();
    this.newTxn(TXN_READONLY, function(error, txn, store){
      if (error)  return;
      let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
                            .getService(Ci.nsIObserverService);
      observerService.notifyObservers(null, TOPIC_DATABASE_READY, null);
    });
  },

  /*
   * return database if it has been created.
   * otherwise, create database, object store and indexes,
   * and then return the database back.
   */
  getDB: function getDB(callback) {
    let self = this;
    if (this.db) {
      callback(null, this.db);
      return;
    }

    let request = mozIndexedDB.open(DB_NAME);
    request.onsuccess = function (event) {
      if (DEBUG)  dump("Opened database: " + DB_NAME +  "\n");
      self.db = event.target.result;
      callback(null, event.target.result);
    };
    request.onupgradeneeded = function (event) {
      if (DEBUG)  dump("Database upgrade: " + DB_NAME + "\n");
      let db = event.target.result;
      let objectStore = db.createObjectStore(STORE_NAME, { keyPath: "manifestURI" });
      objectStore.createIndex("documentURI", "documentURI", { unique: true });
      objectStore.createIndex("lastUpdate", "lastUpdate", { unique: false });
      if (DEBUG) {
        dump("Create object store(" + STORE_NAME + ") and index\n");
        dump("Database upgrade: " + DB_NAME + "\n");
      }
    };
    request.onerror = function (event) {
      callback("Failed to open database.\n", null);
    };
    request.onblocked = function (event) {
      callback("Open request is blocked.\n", null);
    };
  },

  /*
   * make a new transaction request and return it and its objectstore
   *
   * @param txn_type: TXN_READONLY/TXN_READWRITE
   */
  newTxn: function newTxn(txn_type, callback) {
    this.getDB(function (error, db) {
      if (error) {
        dump("Can't open database: " + error + "\n");
        return;
      }
      let txn = db.transaction([STORE_NAME], txn_type);
      let store = txn.objectStore(STORE_NAME);
      callback(null, txn, store);
    });
  },

  /*
   * get all entries from database first, and then check the last
   * update time for each entry.
   */
  update: function update() {
    let updateService = Cc[OFFLINECACHE_UPDATESERVICE_CONTRACTID]
                         .getService(Ci.nsIOfflineCacheUpdateService);
    let self = this;
    this.newTxn(TXN_READONLY, function(error, txn, store){
      if (error)  return;
      let enumRequest = store.openCursor();
      enumRequest.onsuccess = function (event) {
        let cursor = event.target.result;
        let index = self.numEntry;
        // get all entries from the database
        if (cursor) {
          let record = { manifestURI: Services.io.newURI(cursor.key, null, null),
                         documentURI: Services.io.newURI(cursor.value.documentURI, null, null),
                         lastUpdate: cursor.value.lastUpdate };
          self.entries[index] = record;
          Services.perms.add(record.manifestURI, "pin-app", Ci.nsIPermissionManager.ALLOW_ACTION);
          cursor.continue();
          self.numEntry++;
        }
        else{
          // start updating
          let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
                                .getService(Ci.nsIObserverService);
          observerService.notifyObservers(null, TOPIC_APPCACHE_UPDATE, null);
        }
      };
      enumRequest.onerror = function (event) {
        dump("Can't get cursor.\n");
      };
    });
  },

  /*
   * update last update time into database and also property 'entries'
   *
   * @param aLastUpdate:  for database
   * @param aManifestURI: for database
   * @param aDocumentURI: for database
   * @param aIndexUpdate:  for property 'entries'
   */
  updateDB: function updateDB(aLastUpdate, aManifestURI, aDocumentURI, aIndexUpdate) {
    this.entries[aIndexUpdate].lastUpdate = aLastUpdate;
    this.newTxn(TXN_READWRITE, function(error, txn, store){
      if (error)  return;
      let record = {manifestURI: aManifestURI.spec,
                    documentURI: aDocumentURI.spec,
                    lastUpdate: aLastUpdate};
      let updateRequest = store.put(record);
      updateRequest.onsuccess = function(event) {
        let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
                              .getService(Ci.nsIObserverService);
        observerService.notifyObservers(null, TOPIC_DATABASE_UPDATE_COMPLETED, null);
      };
      updateRequest.onerror = function(event) {
        dump("Failed to add record.\n");
      };
    });
  },

  /*
   * observe the scheduled update and notify observers when it has been done
   *
   * @param  update: an update which has been scheduled and may not be finished
   */
  observeUpdate: function observeUpdate(update, callback) {
    let observer = {
      QueryInterface: function QueryInterface(iftype) {
        return this;
      },
      updateStateChanged: function updateStateChanged(update, state) {
        switch(state) {
          case UPDATE_FINISHED:
            callback(new Date().getTime());
            let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
                                 .getService(Ci.nsIObserverService);
            observerService.notifyObservers(null, TOPIC_APPCACHE_UPDATE, null);
            break;
          case UPDATE_ERROR:
            dump("scheduleUpdate returned a error.\n");
            break;
        }
      },
      applicationCacheAvailable: function applicationCacheAvailable(appcache) {
      }
    };
    update.addObserver(observer, false);
    return update;
  },

  /**
   * nsIObserver
   *
   * TOPIC_DATABASE_UPDATE_COMPLETED
   *
   * TOPIC_APPCACHE_UPDATE: If the application cache is expired,
   *   then schedule an update and continue on the next entry when the
   *   update completes.
   */
  observe: function observe(subject, topic, data) {
    switch(topic) {
      case TOPIC_DATABASE_UPDATE_COMPLETED:
        if (DEBUG)  dump("DB has been updated.\n");
        break;
      case TOPIC_APPCACHE_UPDATE:
        let time = new Date().getTime();
        // for each entry, check whether the
        if (this.updateIndex < this.numEntry) {
          if (DEBUG)  dump("update " + this.entries[this.updateIndex].manifestURI + "\n");
          // the application cache is out-dated
          if (time - this.entries[this.updateIndex].lastUpdate > this._updateFrequency) {
            let updateService = Cc[OFFLINECACHE_UPDATESERVICE_CONTRACTID]
                                 .getService(Ci.nsIOfflineCacheUpdateService);
            let update = updateService.scheduleUpdate(this.entries[this.updateIndex].manifestURI,
                                                      this.entries[this.updateIndex].documentURI,
                                                      null);
            let self = this;
            this.observeUpdate(update, function(lastUpdate) {
              if (DEBUG) {
                dump(self.entries[self.updateIndex].manifestURI.spec
                     + " has been updated at "
                     + lastUpdate + "\n");
              }
              self.updateDB(lastUpdate,
                            self.entries[self.updateIndex].manifestURI,
                            self.entries[self.updateIndex].documentURI,
                            self.updateIndex);
              self.updateIndex++;
            });
          }
          else {
            self.updateIndex++;
          }
        }
        else {
          // notify observers that all application cache has been udpated
          let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
                                .getService(Ci.nsIObserverService);
          observerService.notifyObservers(null, TOPIC_APPCACHE_UPDATE_COMPLETED, null);
        }
        break;
    }
  },

  /**
   * nsIApplicationCacheUpdateService
   */
  addEntries: function addEntries(aManifestURI, aDocumentURI) {
    if (DEBUG)  dump("start addEntries()...\n");
    this.newTxn(TXN_READWRITE, function(error, txn, store){
      if (error)  return;
      let record = {manifestURI: aManifestURI.spec,
                    documentURI: aDocumentURI.spec,
                    lastUpdate: new Date().getTime() };
      let addRequest = store.add(record);
      addRequest.onsuccess = function(event) {
        if (DEBUG)  dump("add new record " + record.manifestURI + " " + record.documentURI +"\n");

        let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
                              .getService(Ci.nsIObserverService);
        observerService.notifyObservers(null, TOPIC_DATABASE_ADD_COMPLETED, null);
      };
      addRequest.onerror = function(event) {
        dump("Failed to add record.\n");
      };
    });
  },

  removeEntries: function removeEntries(aManifestURI) {
    if (DEBUG)  dump("start removeEntries()...\n");
    this.newTxn(TXN_READWRITE, function(error, txn, store){
      if (error)  return;
      let removeRequest = store.delete(aManifestURI.spec);
      removeRequest.onsuccess = function(event) {
        if (DEBUG)  dump("remove record " + aManifestURI.spec + "\n");
        let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
                              .getService(Ci.nsIObserverService);
        observerService.notifyObservers(null, TOPIC_DATABASE_REMOVE_COMPLETED, null);
      };
      removeRequest.onerror = function(event) {
        dump("Failed to remove record.\n");
      };
    });
  },

  setUpdateFrequency: function setUpdateFrequency(aHour) {
    this._updateFrequency = aHour * 60 * 60 * 1000;
  },

  enableUpdate: function enableUpdate() {
    if (DEBUG) {
      dump("start enableUpdate()...\n");
      dump("update every " + this._updateFrequency/60/60/1000 + " hour.\n");
    }
    let self = this;
    this.updateTimer.init(
      { observe: function(subject, topic, data) { self.update(); } },
      this._updateFrequency,
      Ci.nsITimer.TYPE_REPEATING_SLACK
    );
  },

  disableUpdate: function disableUpdate() {
    if (DEBUG)  dump("start disableUpdate()...\n");
    this.updateTimer.cancel();
  }
};

const NSGetFactory = XPCOMUtils.generateNSGetFactory([ApplicationCacheUpdateService]);
