for (var i = 0; i < 10000; i++) {
    db = db.getSiblingDB("testdb" + i);
    for(var j=0; j<15; j++ ) {
	    var collName = "testcoll" + j;
	    var doc = {name: "name" + i, seq: j};
	    db.createCollection(collName);        // 创建集合
	    db[collName].createIndex({name: 1});  // 创建索引
	    db[collName].insert(doc);             // 插入一条记录
    }
    print(i);
}
