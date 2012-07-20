"use strict";

let Cc = Components.classes;
let Ci = Components.interfaces;
let Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

const DEBUG = "true";
const DB_NAME = "appcache";
const STORE_NAME = "applications";
const TOPIC_DATABASE_READY = "database-ready";
const TOPIC_DATABASE_ADD_COMPLETED = "database-add-completed";
const TOPIC_DATABASE_REMOVE_COMPLETED = "database-remove-completed";
const TOPIC_DATABASE_UPDATE_COMPLETED = "database-update-completed";
const TOPIC_APPCACHE_UPDATE_COMPLETED = "appcache-update-completed";
const TOPIC_APPCACHE_UPDATE_COMPLETED_ALL = "appcache-update-completed-all";
const TXN_READONLY = "readonly";
const TXN_READWRITE = "readwrite";
const DEBUG = "true";

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
	observerService.addObserver(this, TOPIC_APPCACHE_UPDATE_COMPLETED, false);
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

	init: function init() {
		this.updateTimer = Cc[TIMER_CONTRACTID].createInstance(Ci.nsITimer);
		this.setUpdateFrequency(1);
		this.entries = new Array();
		this.newTxn(TXN_READONLY, function(error, txn, store){
			if (error)	return;

			let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
														.getService(Ci.nsIObserverService);
			observerService.notifyObservers(null, TOPIC_DATABASE_READY, null);
		});
	},

  initDB: function initDB(callback) {
		let self = this;

		if (this.db) {
			callback(null, this.db);
		  return;
		}
		
		let request = mozIndexedDB.open(DB_NAME);
		request.onsuccess = function (event) {
			if (DEBUG)	dump("Opened database: " + DB_NAME +  "\n");

			self.db = event.target.result;
			callback(null, event.target.result);
		};
		request.onupgradeneeded = function (event) {
			if (DEBUG)	dump("Database upgrade: " + DB_NAME + "\n");

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

	newTxn: function newTxn(txn_type, callback) {
		this.initDB(function (error, db) {
			if (error) {
				dump("Can't open database: " + error + "\n");
				return;
			}

			let txn = db.transaction([STORE_NAME], txn_type);
			let store = txn.objectStore(STORE_NAME);
			callback(null, txn, store);
		});
	},

	update: function update() {
		let updateService = Cc[OFFLINECACHE_UPDATESERVICE_CONTRACTID]
                       	.getService(Ci.nsIOfflineCacheUpdateService);

		let self = this;
		this.newTxn(TXN_READONLY, function(error, txn, store){
			if (error)	return;

			let enumRequest = store.openCursor();
			enumRequest.onsuccess = function (event) {
				let cursor = event.target.result;
				let index = self.numEntry;
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
					let time = new Date().getTime();
					if (time - self.entries[self.updateIndex].lastUpdate > self._updateFrequency) {
						dump("request a update for " +  self.entries[self.updateIndex].manifestURI.spec + " ...[" + time + "] [" + self.entries[self.updateIndex].lastUpdate+ "] ["+ (time-self.entries[self.updateIndex].lastUpdate) + "]\n");
						let update = updateService.scheduleUpdate(self.entries[self.updateIndex].manifestURI, 
																											self.entries[self.updateIndex].documentURI, 
																											null);
						self.watchUpdate(update, function(lastUpdate) {
							dump(self.entries[self.updateIndex].manifestURI.spec + " has been updated at " + lastUpdate + "\n");
							self.updateDB(lastUpdate, 
														self.entries[self.updateIndex].manifestURI,
														self.entries[self.updateIndex].documentURI,
														self.updateIndex );
							self.updateIndex++;
						});
					}
					else {
						self.updateIndex++;
					}
				}
			};
			enumRequest.onerror = function (event) {
				dump("Can't get cursor.\n");
			};
		});
	},

	updateDB: function updateDB(aLastUpdate, aManifestURI, aDocumentURI, aIndexUpdate) {
		this.entries[aIndexUpdate].lastUpdate = aLastUpdate;
    this.newTxn(TXN_READWRITE, function(error, txn, store){
      if (error)  return;
      let record = {manifestURI: aManifestURI.spec,
                    documentURI: aDocumentURI.spec,
										lastUpdate: aLastUpdate};
      let updateRequest = store.put(record);
      updateRequest.onsuccess = function(event) {
        dump("update record " + record.manifestURI + " " + record.documentURI + " " + record.lastUpdate +"\n");
        let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
                              .getService(Ci.nsIObserverService);
        observerService.notifyObservers(null, TOPIC_DATABASE_UPDATE_COMPLETED, null);
      };
      updateRequest.onerror = function(event) {
        dump("Failed to add record.\n");
      };
    });


	},

	watchUpdate: function watchUpdate(update, callback) {
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
			      observerService.notifyObservers(null, TOPIC_APPCACHE_UPDATE_COMPLETED, null);
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
   */
	observe: function observe(subject, topic, data) {
		dump("got notification of " + topic + "\n");
		switch(topic) {
			case TOPIC_DATABASE_UPDATE_COMPLETED:
				dump("DB has been updated.\n");
				break;
			case TOPIC_APPCACHE_UPDATE_COMPLETED:
				if (this.updateIndex < this.numEntry) {
					if (new Date().getTime() - self.entries[self.updateIndex].lastUpdate > self._updateFrequency) {
						let updateService = Cc[OFFLINECACHE_UPDATESERVICE_CONTRACTID]
							                 	.getService(Ci.nsIOfflineCacheUpdateService);
						let update = updateService.scheduleUpdate(this.entries[this.updateIndex].manifestURI, 
																											this.entries[this.updateIndex].documentURI, 
																											null);
						let self = this;
						this.watchUpdate(update, function(lastUpdate) {
							dump(self.entries[self.updateIndex].manifestURI.spec + " has been updated at " + lastUpdate + "\n");
							self.updateDB(lastUpdate, self.entries[self.updateIndex].manifestURI, self.entries[self.updateIndex].documentURI, self.updateIndex);
							self.updateIndex++;
						});
					}
					else {
						self.updateIndex++;
					}
				}
				else {
			    let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
						                    .getService(Ci.nsIObserverService);
					observerService.notifyObservers(null, TOPIC_APPCACHE_UPDATE_ALL_COMPLETED, null);	
				}
				break;
		}
	},
  
	/**
   * nsIApplicationCacheUpdateService API
   */
	addEntries: function addEntries(aManifestURI, aDocumentURI) {
		dump("start addEntries()...\n");

    this.newTxn(TXN_READWRITE, function(error, txn, store){
      if (error)	return;

			let record = {manifestURI: aManifestURI.spec, 
										documentURI: aDocumentURI.spec,
										lastUpdate: new Date().getTime() };
      let addRequest = store.add(record);
			addRequest.onsuccess = function(event) {
				dump("add new record " + record.manifestURI + " " + record.documentURI +"\n");
				
				let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
															.getService(Ci.nsIObserverService);
				observerService.notifyObservers(null, TOPIC_DATABASE_ADD_COMPLETED, null);
			};
			addRequest.onerror = function(event) {
				dump("Failed to add record.\n");
			};
    });
    dump("end of addEntries().\n");
	},

	removeEntries: function removeEntries(aManifestURI) {
		dump("start removeEntries()...\n");

		this.newTxn(TXN_READWRITE, function(error, txn, store){
			if (error)  return;

      let removeRequest = store.delete(aManifestURI.spec);
      removeRequest.onsuccess = function(event) {
        dump("remove record " + aManifestURI.spec + "\n");

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
		let self = this;
		dump("start enableUpdate()...\n");
		dump("update every " + this._updateFrequency/60/60/1000 + " hour.\n");
		this.updateTimer.init(
			{ observe: function(subject, topic, data) { self.update(); dump("timer:[" + new Date().getTime() + "]\n"); } },
			this._updateFrequency + 500,
			Ci.nsITimer.TYPE_REPEATING_SLACK
		);
	},

	disableUpdate: function disableUpdate() {
		dump("start disableUpdate()...\n");
		this.updateTimer.cancel();
	} 
};

const NSGetFactory = XPCOMUtils.generateNSGetFactory([ApplicationCacheUpdateService]);
