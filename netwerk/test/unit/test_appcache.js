do_load_httpd_js();

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

const TOPIC_DATABASE_READY = "database-ready";
const TOPIC_DATABASE_ADD_COMPLETED = "database-add-completed";
const TOPIC_DATABASE_REMOVE_COMPLETED = "database-remove-completed";
const TOPIC_APPCACHE_UPDATE_COMPLETED_ALL = "appcache-update-completed-all";

const OBSERVERSERVICE_CONTRACTID = "@mozilla.org/observer-service;1";
const APPLICATIONCACHE_UPDATESERVICE_CONTRACTID = "@mozilla.org/network/applicationcacheupdateservice;1";

const kManifest1 = "CACHE MANIFEST\n" +
  "/pages/foo1\n" +
  "/pages/foo2\n" +
  "/pages/foo3\n" +
  "/pages/foo4\n";
const kManifest2 = "CACHE MANIFEST\n" +
  "/pages/foo5\n" +
  "/pages/foo6\n" +
  "/pages/foo7\n" +
  "/pages/foo8\n";

function manifest1_handler(metadata, response) {
	dump("manifest1\n");
  response.setHeader("content-type", "text/cache-manifest");

  response.write(kManifest1);
}

function manifest2_handler(metadata, response) {
  dump("manifest2\n");
  response.setHeader("content-type", "text/cache-manifest");

  response.write(kManifest2);
}

function app_handler(metadata, response) {
  dump("app_handler\n");
  response.setHeader("content-type", "text/html");

  response.write("<html></html>");
}

function datafile_handler(metadata, response) {
  dump("datafile_handler\n");
  let data = "";

  while(data.length < 1024) {
    data = data + Math.random().toString(36).substring(2, 15);
  }

  response.setHeader("content-type", "text/plain");
  response.write(data.substring(0, 1024));
}

var httpServer;

function ApplicationCacheUpdateObserver(tcase) {
  let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
                        .getService(Ci.nsIObserverService);
  observerService.addObserver(this, TOPIC_DATABASE_READY, false);
  observerService.addObserver(this, TOPIC_DATABASE_ADD_COMPLETED, false);
  observerService.addObserver(this, TOPIC_DATABASE_REMOVE_COMPLETED, false);
  observerService.addObserver(this, TOPIC_APPCACHE_UPDATE_COMPLETED_ALL, false);
	
	this.updateService = Cc[APPLICATIONCACHE_UPDATESERVICE_CONTRACTID]
											.getService(Ci.nsIApplicationCacheUpdateService);

	this.test_case = tcase;
}

ApplicationCacheUpdateObserver.prototype = {
	test_case: null,
	updateService: null,
	req_num: 1,
	flag1: true,
	flag2: true,

  observe: function observe(subject, topic, data) {
		dump("observer got notification!\n");
		let manifestURI;
		let documentURI;
		switch (topic) {
			case TOPIC_DATABASE_READY:
				manifestURI = Services.io.newURI("http://example.com", null, null);
				this.updateService.addEntries(manifestURI, manifestURI);
				manifestURI = Services.io.newURI("http://127.0.0.1:4444/app1.appcache", null, null);
	  		documentURI = Services.io.newURI("http://127.0.0.1:4444/app1", null, null);
				this.updateService.addEntries(manifestURI, documentURI);
				manifestURI = Services.io.newURI("http://127.0.0.1:4444/app2.appcache", null, null);
				documentURI = Services.io.newURI("http://127.0.0.1:4444/app2", null, null);
				this.updateService.addEntries(manifestURI, documentURI);
				break;
			case TOPIC_DATABASE_ADD_COMPLETED:
				if (this.flag1) {
					manifestURI = Services.io.newURI("http://example.com", null, null);
					this.updateService.removeEntries(manifestURI);
					this.flag1 = false;
				}
				break;
			case TOPIC_DATABASE_REMOVE_COMPLETED:
				if (this.flag2) {
					this.updateService.enableUpdate();
					this.flag2 = false;
				}
				break;
			case TOPIC_APPCACHE_UPDATE_COMPLETED_ALL:
				dump("all entries has been updated. bye!\n");
//				do_test_finished();
				break;
		}
	}
}

function start_add_app() {
	dump("Start add app\n");
	new ApplicationCacheUpdateObserver("addEntries");
}

function init_http_server() {
  httpServer = new nsHttpServer();
  httpServer.registerPathHandler("/app1", app_handler);
  httpServer.registerPathHandler("/app2", app_handler);
  httpServer.registerPathHandler("/app1.appcache", manifest1_handler);
  httpServer.registerPathHandler("/app2.appcache", manifest2_handler);
  for (i = 1; i <= 8; i++) {
    httpServer.registerPathHandler("/pages/foo" + i, datafile_handler);
  }
  httpServer.start(4444);
}

function init_profile() {
  var ps = Cc["@mozilla.org/preferences-service;1"]
    .getService(Ci.nsIPrefBranch);
  dump(ps.getBoolPref("browser.cache.offline.enable") + "\n");
  ps.setBoolPref("browser.cache.offline.enable", true);
  ps.setComplexValue("browser.cache.offline.parent_directory",
         Ci.nsILocalFile, do_get_profile());
}

function init_cache_capacity() {
  let prefs = Cc["@mozilla.org/preferences-service;1"]
    .getService(Components.interfaces.nsIPrefBranch);
  prefs.setIntPref("browser.cache.offline.capacity", 1024 * 5 / 1024);
}

function clean_app_cache() {
  evict_cache_entries(Ci.nsICache.STORE_OFFLINE);
}

function run_test() {
  if (typeof _XPCSHELL_PROCESS == "undefined" ||
      _XPCSHELL_PROCESS != "child") { 
    init_profile();
    init_cache_capacity();
    clean_app_cache();
  }

	init_http_server();
	start_add_app();
	do_test_pending();
}

