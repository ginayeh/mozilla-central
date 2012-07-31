var Cu = Components.utils;
var Cc = Components.classes;
var Ci = Components.interfaces;
var Cr = Components.results;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/Webapps.jsm");

function AppsUpdater() {
	this.timer = null;
	this.init();	
}

AppsUpdater.prototype = {
	init: function init() {
    this.timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    this.timer.initWithCallback(this, 500, this.timer.TYPE_REPEATING_SLACK);
	},

	stop: function stop() {
		this.timer.cancel();
		this.timer = null;
	},

  // This function is called by the Update Timer Manager for us.
  // We need to update packaged apps and apps that use the offline cache.
  notify: function appsUpdater_notify(aTimer) {
		dump("got notified!\n");
    // Don't do anything if we're offline.
    // XXX : check that we're on wifi/wired/3G+ connection?
//    if (Services.io.offline) {
//      return;
//    }

    let apps = DOMApplicationRegistry.webapps;
		dump("apps' length: " + apps.length + "\n");

    for (let id in apps) {
      if (apps[id].packageURL) {
        this.updatePackagedApp(apps[id]);
      } else {
				dump("apps[" + id + "]: \n");
        // TODO: test is this app manifest references an offline cache, and update it.
          for (let prop in apps[id]) {
            dump(prop + ": "+ apps[id][prop]);
          }
      }
    }
  },

  updatePackagedApp: function appsUpdater_updatePackage(aApp) {
    // XXX Should we have a "Do you want to update" prompt here?
    let data = {
      url: aApp.packageURL,
      installOrigin: aApp.installOrigin,
      receipts: aApp.receipts,
      etag: aApp.etag
    }
    DOMApplicationRegistry.installPackage(data, true);
  }
}

function run_test() {
	var apps_updater = new AppsUpdater();
	do_test_pending();
}
