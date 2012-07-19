//var idbManager = Components.classes["@mozilla.org/dom/indexeddb/manager;1"].
//	               getService(Components.interfaces.nsIIndexedDatabaseManager);
//idbManager.initWindowless(this);
const dbName = "MyTestDatabase";
var db;

function run_test() {
	run_next_test();
}

add_test(function initDB() {
	const customerData = [
	  { ssn: "444-44-4444", name: "Bill", age: 35, email: "bill@company.com"},
	  { ssn: "555-55-5555", name: "Donna", age: 32, email: "donna@home.org"}
	];

	var openRequest = mozIndexedDB.open(dbName, 1);
	openRequest.onsuccess = function(evt) {
		dump("Database open success.\n");
		db = openRequest.result;
		do_test_pending();	
		run_next_test(); 
		do_test_finished();
	};
	openRequest.onerror = function(evt) {
		dump("Database error: " + evt.target.error.name + "\n");
	};
  openRequest.onupgradeneeded = function(evt) {
		dump("Database upgrade.\n");
		db = evt.target.result;

		var objectStore = db.createObjectStore("customers", { keyPath: "ssn"});

		objectStore.createIndex("name", "name", { unique: false});
		objectStore.createIndex("email", "email", { unique: true});

		for (var i in customerData) {
			dump("Add new record.\n");
			objectStore.add(customerData[i]);
		}
	};
});

add_test(function getData() {
	var transaction = db.transaction(["customers"]);
	var objectStore = transaction.objectStore("customers");
	var getRequest = objectStore.get("444-44-4444");
	getRequest.onerror = function(evt) {
		dump("Get error: " + evt.target.error.name + "\n");
	};
	getRequest.onsuccess = function(evt) {
		dump("Get success: " + getRequest.result.name);
		do_test_pending();
		run_next_test();
		do_test_finished();
	}
});

add_test(function getAllData() {
	var transaction = db.transaction(["customers"]);
	var objectStore = transaction.objectStore("customers");
  var getAllRequest =	objectStore.openCursor();
	getAllRequest.onsuccess = function(evt) {
		dump("Get cursor success: ");
		var cursor = evt.target.result;
		if (cursor) {
			dump(cursor.key + " " + cursor.value.name + "\n");
			cursor.continue();
		}
		else {
			dump("No more entries!\n");
			do_test_pending();
			run_next_test();
			do_test_finished();
		}
	};
	getAllRequest.onerror = function(evt) {
		dump("Get cursor error.");
	}; 
});

add_test(function searchData() {
	var transaction = db.transaction(["customers"]);
	var objectStore = transaction.objectStore("customers");
	var index = objectStore.index("name");
	var searchRequest = index.get("Donna");
	searchRequest.onsuccess = function(evt) {
		dump("Search success: " + evt.target.result.email + " " + evt.target.result.age + "\n");
		do_test_finished();	
	};
	searchRequest.onerror = function(evt) {
		dump("Cannot find.\n");
	};	
});
