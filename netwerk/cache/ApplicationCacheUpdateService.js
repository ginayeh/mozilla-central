"use strict";

let Cc = Components.classes;
let Ci = Components.interfaces;
let Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

const DB_NAME = "appcache";
const STORE_NAME = "applications";
const TOPIC_DATABASE_READY = "database-ready";
const TOPIC_DATABASE_ADD_COMPLETED = "database-add-completed";
const TOPIC_DATABASE_REMOVE_COMPLETED = "database-remove-completed";
const TXN_READONLY = "readonly";
const TXN_READWRITE = "readwrite";
const DEBUG = "true";

const appcaches = [
	{manifestURI: "URI1", documentURI:"URI3"},
	{manifestURI: "URI2", documentURI:"URI4"}
];

const APPLICATIONCACHE_UPDATESERVICE_CONTRACTID = "@mozilla.org/network/applicationcacheupdateservice;1";
const APPLICATIONCACHE_UPDATESERVICE_CID = Components.ID("{ac83ae97-69a0-4217-9c1f-0e3d47973b84}");
const OFFLINECACHE_UPDATESERVICE_CONTRACTID = "@mozilla.org/offlinecacheupdate-service;1";
const OBSERVERSERVICE_CONTRACTID = "@mozilla.org/observer-service;1";
const TIMER_CONTRACTID = "@mozilla.org/timer;1";

var idbManager = Components.classes["@mozilla.org/dom/indexeddb/manager;1"]
								 .getService(Components.interfaces.nsIIndexedDatabaseManager);
idbManager.initWindowless(this);

function ApplicationCacheUpdateService() {
	if (DEBUG)	dump("register observer\n");
	let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
													.getService(Ci.nsIObserverService);
	observerService.addObserver(this, TOPIC_DATABASE_READY, false);
	this.init();
}

ApplicationCacheUpdateService.prototype = {
	classID: APPLICATIONCACHE_UPDATESERVICE_CID,
	contractID: APPLICATIONCACHE_UPDATESERVICE_CONTRACTID,
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIApplicationCacheUpdateService,
                                         Ci.nsIObserver]),
	db: null,

	set updateFrequency(aUpdateFrequency) {
		this._updateFrequency = aUpdateFrequency * 60 * 60 * 1000;
		dump("set updateFrequency to (" + this._updateFrequency + ")\n");
	},

	get updateFrequency() {
		return this._updateFrequency;
	},

	updateTimer: null,

	init: function init() {
		dump("init()\n");

		this.updateTimer = Cc[TIMER_CONTRACTID].createInstance(Ci.nsITimer);
		this._updateFrequency = 1 * 60 * 60 * 1000;

		this.newTxn(TXN_READONLY, function(error, txn, store){
			if (error)	return;

			let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
														.getService(Ci.nsIObserverService);
			observerService.notifyObservers(null, TOPIC_DATABASE_READY, null);
		});
		dump("end of init().\n");
	},

  initDB: function initDB(callback) {
		let self = this;


		if (this.db) {
			dump("database existed. go back.\n");
			callback(null, this.db);
		  return;
		}
		
		let request = mozIndexedDB.open(DB_NAME);
		request.onsuccess = function (event) {
			dump("Opened database: " + DB_NAME +  "\n");
			self.db = event.target.result;
			callback(null, event.target.result);
		};
		request.onupgradeneeded = function (event) {
			dump("Database upgrade: " + DB_NAME + "\n");
			let db = event.target.result;
			
			let objectStore = db.createObjectStore(STORE_NAME, { keyPath: "manifestURI" });
			objectStore.createIndex("documentURI", "documentURI", { unique: true });
			dump("Create object store(" + STORE_NAME + ") and index\n");
		};
		request.onerror = function (event) {
			dump("go back: onerror: " + event.target.error.name + "\n");
			callback("Failed to open database.\n", null);
		};
		request.onblocked = function (event) {
			dump("go back: on blocked\n");
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
		dump("start update()...\n");
		let updateService = Cc[OFFLINECACHE_UPDATESERVICE_CONTRACTID]
                       	.getService(Ci.nsIOfflineCacheUpdateService);
		
		this.newTxn(TXN_READONLY, function(error, txn, store){
			if (error)	return;

			let enumRequest = store.openCursor();
			enumRequest.onsuccess = function (event) {
				let cursor = event.target.result;
				if (cursor) {
					dump(cursor.key + "\t" + cursor.value.documentURI + "\n");
					updateService.scheduleUpdate(cursor.key, cursor.value.documentURI, null);
					dump("next\n");
					cursor.continue();
				}	
				else	dump("no more entries.\n");
			};
			request.onerror = function (event) {
				dump("Can't get cursor.\n");
			};
		});
	},

  /**
   * nsIObserver
   */
	observe: function observe(subject, topic, data) {
		dump("got notification of " + topic + "\n");
		switch(topic) {
			case TOPIC_DATABASE_READY:
			//	dump("got notification of " + topic + "\n");
				break;
			case TOPIC_DATABASE_ADD_COMPLETED:
				break;
			case TOPIC_DATABASE_REMOVE_COMPLETED:
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
										documentURI: aDocumentURI.spec};
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
      };
      removeRequest.onerror = function(event) {
        dump("Failed to remove record.\n");
      };
		});
	},
  
  enableUpdate: function enableUpdate() {
		let self = this;
		dump("start enableUpdate()...\n");
		dump("update every " + this._updateFrequency + "mSec.\n");
		this.updateTimer.initWithCallback(
			{ notify: function(timer) { self.update(); } },
			this._updateFrequency,
			Ci.nsITimer.TYPE_REPEATING_SLACK
		);
	},

	disableUpdate: function disableUpdate() {
		dump("start disableUpdate()...\n");
		this.updateTimer.cancel();
	} 
};

const NSGetFactory = XPCOMUtils.generateNSGetFactory([ApplicationCacheUpdateService]);
