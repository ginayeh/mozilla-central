"use strict";

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
//Cu.import("resource://gre/modules/Services.jsm");

const DEBUG = true;
const DB_NAME = "appcache";
const DB_VERSION = 1;
const STORE_NAME = "applications";

const appcaches = [
	{manifestURI: "URI1"},
	{manifestURI: "URI2"}
];

const APPLICATIONCACHE_UPDATESERVICE_CONTRACTID = "@mozilla.org/network/applicationcacheupdateservice;1";
const APPLICATIONCACHE_UPDATESERVICE_CID = Components.ID("{ac83ae97-69a0-4217-9c1f-0e3d47973b84}");
const OFFLINECACHE_UPDATESERVICE_CONTRACEID = "@mozilla.org/offlinecacheupdate-service;1";

var idbManager = Components.classes["@mozilla.org/dom/indexeddb/manager;1"]
								 .getService(Components.interfaces.nsIIndexedDatabaseManager);
idbManager.initWindowless(this);

function ApplicationCacheUpdateService() {
	dump("register\n");
//	this.register();
}

ApplicationCacheUpdateService.prototype = {
	classID: APPLICATIONCACHE_UPDATESERVICE_CID,
	contractID: APPLICATIONCACHE_UPDATESERVICE_CONTRACTID,
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIApplicationCacheUpdateService]),
//                                         Ci.nsIObserver]),	

	db: null,

  /** 
   *  
   */
	init: function init() {
		dump("init()\n");
		this.newTxn(function(error, txn, store){
			dump("no name function(1)\n");
			if (error)	return;

			let request = store.openCursor();
			request.onsuccess = function (event) {
				let cursor = event.target.result;
				if (cursor) {
					dump(cursor.key + "\n");
					cursor.continue();
				}	
				else {
					dump("no more entries.\n");
				}
			};
			request.onerror = function (event) {
				dump("Can't get cursor.\n");
			};
			dump("end no name function(1)\n");
		});
		dump("end of init().\n");
	},

  initDB: function initDB(callback) {
		if (this.db) {
			dump("database existed. go back.\n");
			callback(null, this.db);
		  return;
		}
		
		let request = mozIndexedDB.open(DB_NAME, 1);
		request.onsuccess = function (event) {
			dump("Opened database: " + DB_NAME +  "\n");
			this.db = event.target.result;
			callback(null, event.target.result);
		};
		request.onupgradeneeded = function (event) {
			dump("Database upgrade: " + DB_NAME + "\n");
			let db = event.target.result;
			
			let objectStore = db.createObjectStore(STORE_NAME, { keyPath: "manifestURI" });
			objectStore.createIndex("manifestURI", "manifestURI", { unique: true });
			dump("Create object store(" + STORE_NAME + ") and index\n");

			for (var i in appcaches) {
				dump("Add new record.\n");
				objectStore.add(appcaches[i]);
			}
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

	newTxn: function newTxn(callback) {
		this.initDB(function (error, db) {
			dump("no name function(2)\n");
			if (error) {
				dump("Can't open database: " + error + "\n");
				return;
			}
			let txn = db.transaction([STORE_NAME]);
			let store = txn.objectStore(STORE_NAME);
			callback(null, txn, store);
			dump("end no name function(2)\n");
		});
	},
/*
	update: function update() {
		let updateService = Cc[OFFLINECACHE_UPDATESERVICE_CONTRACEID]
                       	.getService(Ci.nsIOfflineCacheUpdateService);
		updateService.scheduleUpdate(manifestURI, documentURI, null);
	}
*/

  /**
   * nsIObserver
   */
/*
	observe: function observe(subject, topic, data) {
		switch(topic) {
			case "applicationcache-update":
				dump("got notification.\n");
				break;
		}
	},
  register: function() {
    let observerService = Cc["@mozilla.org/observer-service;1"]
                          .getService(Ci.nsIObserverService);
    observerService.addObserver(this, "applicationcache-update", false);
  },
  unregister: function() {
    let observerService = Cc["@mozilla.org/observer-service;1"]
                          .getService(Ci.nsIObserverService);
    observerService.removeObserver(this, "applicationcache-update");
  },
*/
  /**
   * nsIApplicationCacheUpdateService API
   */

	addEntries: function addEntries(manifestURI, documentURI) {
		dump("add entry");
//		Services.obs.notifyObservers("add entry", "applicationcache-update", null);
		this.newTxn(function(error, txn, store){
			if (error)	return;

			let index = store.index("manifestURI");
			let request = index.get("URI2");
			request.onsuccess = function (event) {
				dump("Search success: " + event.target.result.key + "\n");
			};
			request.onerror = function (event) {	
				dump("Cannot find.\n");
			};
			dump("end no name function(1)\n");
		});
	},

	removeEntries: function removeEntries(manifestURI) {
		dump("remove entry.\n");
//		Services.obs.notifyObservers("remove entry", "applicationcache-update", null);
		this.init();
	},
  
	setUpdateFrequency: function setUpdateFrequency(second) {
		dump("set up frequency\n");
	},

  enableUpdate: function enableUpdate() {
		dump("enable service\n");
	},

	disableUpdate: function disableUpdate() {
		dump("disable service\n");
	} 
};

const NSGetFactory = XPCOMUtils.generateNSGetFactory([ApplicationCacheUpdateService]);
