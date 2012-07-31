"use strict";

let Cc = Components.classes;
let Ci = Components.interfaces;
let Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

let EXPORTED_SYMBOLS = ["", ""];

const DEBUG = true;
const TOPIC_DATABASE_ADD_COMPLETED = "database-add-completed";
const TOPIC_DATABASE_REMOVE_COMPLETED = "database-remove-completed";
const TOPIC_DATABASE_UPDATE_COMPLETED = "database-update-completed";
const TOPIC_APPCACHE_UPDATE = "appcache-update";
const TOPIC_APPCACHE_UPDATE_COMPLETED = "appcache-update-completed";
const UPDATE_FINISHED = Ci.nsIOfflineCacheUpdateObserver.STATE_FINISHED;
const UPDATE_ERROR = Ci.nsIOfflineCacheUpdateObserver.STATE_ERROR;
const UPDATE_CHECKING = Ci.nsIOfflineCacheUpdateObserver.STATE_CHECKING;
const OFFLINECACHE_UPDATESERVICE_CONTRACTID = "@mozilla.org/offlinecacheupdate-service;1";
const OBSERVERSERVICE_CONTRACTID = "@mozilla.org/observer-service;1";

function ApplicationCacheUpdateService() {
  let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
                        .getService(Ci.nsIObserverService);
  observerService.addObserver(this, TOPIC_APPCACHE_UPDATE, false);
  this.init();
}

ApplicationCacheUpdateService.prototype = {
  updateIndex: 0,
  numEntry: 0,
  entries: [],

  /*
   * initialization
   */
  init: function init() {
    this.entries = new Array();
  },

  /*
   * get all entries from database first, and then check the last
   * update time for each entry.
   */
  update: function update() {
    let updateService = Cc[OFFLINECACHE_UPDATESERVICE_CONTRACTID]
                         .getService(Ci.nsIOfflineCacheUpdateService);
    let self = this;

					// get all entries
		      let index = self.numEntry;
          let record = { manifestURI: Services.io.newURI(cursor.key, null, null),
                         documentURI: Services.io.newURI(cursor.value.documentURI, null, null),
                         lastUpdate: cursor.value.lastUpdate };
          self.entries[index] = record;
          Services.perms.add(record.manifestURI, "pin-app", Ci.nsIPermissionManager.ALLOW_ACTION);
          self.numEntry++;
          
					// start updating
          let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
                                .getService(Ci.nsIObserverService);
          observerService.notifyObservers(null, TOPIC_APPCACHE_UPDATE, null);
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
  }
};

const NSGetFactory = XPCOMUtils.generateNSGetFactory([ApplicationCacheUpdateService]);
