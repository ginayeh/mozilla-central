"use strict";

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

//const IDBTransaction = Ci.nsIIDBTransaction;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

const DEBUG = true;
const DB_NAME = "appcache";

const DB_VERSION = 1;
const STORE_NAME = "applications";
const TOPIC_DATABASE_READY = "database-ready";
const TXN_READONLY = "readonly";
const TXN_READWRITE = "readwrite";

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
	dump("register\n");
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
	mUpdateFrequency: null,	
	mUpdateTimer: null,

  /** 
   *  
   */
	init: function init() {
		dump("init()\n");

		this.mUpdateTimer = Cc[TIMER_CONTRACTID].createInstance(Ci.nsITimer);
		this.setUpdateFrequency(300);

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
		
		let request = mozIndexedDB.open(DB_NAME, 1);
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
		switch(topic) {
			case TOPIC_DATABASE_READY:
				dump("got notification of " + topic + "\n");

				dump("call addEntries...\n");
				let manifestURI = Services.io.newURI("http://example.com", null, null);
				this.addEntries(manifestURI, manifestURI);
				
				manifestURI = Services.io.newURI("http://home.org", null, null);
				this.addEntries(manifestURI, manifestURI);

/*
				dump("call removeEntries...\n");
				this.removeEntries(manifestURI);   */	
		
//				this.setUpdateFrequency(60);
				this.enableUpdate();
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
  
	setUpdateFrequency: function setUpdateFrequency(aSecond) {
		this.mUpdateFrequency = aSecond * 1000;
		dump("setUpdateFrequency(" + this.mUpdateFrequency + ")\n");
	},

  enableUpdate: function enableUpdate() {
		let self = this;
		dump("start enableUpdate()...\n");
		dump("update every " + this.mUpdateFrequency + "mSec.\n");
		this.mUpdateTimer.initWithCallback(
			{ notify: function(timer) { self.update(); } },
			this.mUpdateFrequency,
			Ci.nsITimer.TYPE_REPEATING_SLACK
		);
	},

	disableUpdate: function disableUpdate() {
		dump("start disableUpdate()...\n");
		this.mUpdateTimer.cancel();
	} 
};

const NSGetFactory = XPCOMUtils.generateNSGetFactory([ApplicationCacheUpdateService]);
