do_load_httpd_js();

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

const TOPIC_DATABASE_READY = "database-ready";
const TOPIC_DATABASE_ADD_COMPLETED = "database-add-completed";
const TOPIC_DATABASE_REMOVE_COMPLETED = "database-remove-completed";
const TOPIC_APPCACHE_UPDATE_COMPLETED = "appcache-update-completed";

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
  do_print("manifest1\n");
  response.setHeader("content-type", "text/cache-manifest");

  response.write(kManifest1);
}

function manifest2_handler(metadata, response) {
  do_print("manifest2\n");
  response.setHeader("content-type", "text/cache-manifest");

  response.write(kManifest2);
}

function app_handler(metadata, response) {
  do_print("app_handler\n");
  response.setHeader("content-type", "text/html");

  response.write("<html></html>");
}

function datafile_handler(metadata, response) {
  do_print("datafile_handler\n");
  let data = "";

  while(data.length < 1024) {
    data = data + Math.random().toString(36).substring(2, 15);
  }

  response.setHeader("content-type", "text/plain");
  response.write(data.substring(0, 1024));
}

var httpServer;

function ApplicationCacheUpdateObserver() {
  let observerService = Cc[OBSERVERSERVICE_CONTRACTID]
                        .getService(Ci.nsIObserverService);
  observerService.addObserver(this, TOPIC_DATABASE_READY, false);
  observerService.addObserver(this, TOPIC_DATABASE_ADD_COMPLETED, false);
  observerService.addObserver(this, TOPIC_DATABASE_REMOVE_COMPLETED, false);
  observerService.addObserver(this, TOPIC_APPCACHE_UPDATE_COMPLETED, false);

  this.updateService = Cc[APPLICATIONCACHE_UPDATESERVICE_CONTRACTID]
                      .getService(Ci.nsIApplicationCacheUpdateService);
}

ApplicationCacheUpdateObserver.prototype = {
  updateService: null,
  add_completed: 0,
  start_enable_test: true,

  observe: function observe(subject, topic, data) {
    switch (topic) {
      case TOPIC_DATABASE_READY:
        dump(topic+"\n");
        this.start_add_app();
        break;
      case TOPIC_DATABASE_ADD_COMPLETED:
        this.add_completed++;
        this.start_remove_app();
        break;
      case TOPIC_DATABASE_REMOVE_COMPLETED:
        this.start_enable_update();
        break;
      case TOPIC_APPCACHE_UPDATE_COMPLETED:
        this.start_disable_update();
        do_print("all entries has been updated. bye!\n");
        do_test_finished();
        break;
    }
  },

  start_add_app: function start_add_app() {
    do_print("start_add_app");
    let manifestURI = Services.io.newURI("http://example.com", null, null);
    this.updateService.addEntries(manifestURI, manifestURI);
    manifestURI = Services.io.newURI("http://127.0.0.1:4444/app1.appcache", null, null);
    let documentURI = Services.io.newURI("http://127.0.0.1:4444/app1", null, null);
    this.updateService.addEntries(manifestURI, documentURI);
    manifestURI = Services.io.newURI("http://127.0.0.1:4444/app2.appcache", null, null);
    documentURI = Services.io.newURI("http://127.0.0.1:4444/app2", null, null);
    this.updateService.addEntries(manifestURI, documentURI);
  },

  start_remove_app: function start_remove_app() {
    if (this.add_completed == 3) {
      do_print("start_remove_app");
      let manifestURI = Services.io.newURI("http://example.com", null, null);
      this.updateService.removeEntries(manifestURI);
      this.start_remove_test = false;
    }
  },

  start_enable_update: function start_update() {
    do_print("start_enable_update");
    this.updateService.enableUpdate();
    this.flag2 = start_enable_test;
  },

  start_disable_update: function start_disable_update() {
    do_print("start_disable_update");
    this.updateService.disableUpdate();
  }
}

function init_database() {
  do_print("init database\n");
  new ApplicationCacheUpdateObserver();
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
  init_database();
  do_test_pending();
}

