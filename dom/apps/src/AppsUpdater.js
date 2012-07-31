/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Cu = Components.utils;
const Cc = Components.classes;
const Ci = Components.interfaces;
const Cr = Components.results;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/Webapps.jsm");

function AppsUpdater() {
}
 
AppsUpdater.prototype = {
 
	// This function is called by the Update Timer Manager for us.
	// We need to update packaged apps and apps that use the offline cache.
	notify: function appsUpdater_notify(aTimer) {
		// Don't do anything if we're offline.
		// XXX : check that we're on wifi/wired/3G+ connection?
		if (Services.io.offline) {
			return;
		}

		let apps = DOMApplicationRegistry.webapps;

		for (let id in apps) {
			if (apps[id].packageURL) {
				this.updatePackagedApp(apps[id]);
			} else {
				// TODO: test is this app manifest references an offline cache, and update it.
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
	},

	classID : Components.ID("{7b828555-b0e6-4592-95e2-a566c0d1851e}"),
	QueryInterface : XPCOMUtils.generateQI([Ci.nsITimerCallback])
}

const NSGetFactory = XPCOMUtils.generateNSGetFactory([AppsUpdater]);
