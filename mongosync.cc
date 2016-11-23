#include "mongosync.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <string.h>
#include <string>

#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "util.h"
#include "mongo/util/mongoutils/str.h"

static void GetSeparateArgs(const std::string& raw_str, std::vector<std::string>* argv_p) {
  const char* p = raw_str.data();
  const char *pch = strtok(const_cast<char*>(p), " ");
  while (pch != NULL) {
    argv_p->push_back(std::string(pch));
    pch = strtok(NULL, " ");
  }
}

static bool before(const OplogTime& t1, const OplogTime& t2) {
	return *reinterpret_cast<const uint64_t*>(&t1) < *reinterpret_cast<const uint64_t*>(&t2);	
}

static void Usage() {
	std::cerr << "Follow is the mongosync-surpported options: " << std::endl;	
	std::cerr << "--help                   to get the help message" << std::endl;
	std::cerr << "--src_srv arg            the source mongodb server's ip port" << std::endl;
	std::cerr << "--src_user arg           the source mongodb server's logging user" << std::endl;
	std::cerr << "--src_passwd arg         the source mongodb server's logging password" << std::endl;
	std::cerr << "--src_auth_db arg        the source mongodb server's auth db" << std::endl;
	std::cerr << "--dst_srv arg            the destination mongodb server's ip port" << std::endl;
	std::cerr << "--dst_user arg           the destination mongodb server's logging user" << std::endl;
	std::cerr << "--dst_passwd arg         the destination mongodb server's logging password" << std::endl;
	std::cerr << "--dst_auth_db arg        the destination mongodb server's auth db" << std::endl;
	std::cerr << "--db arg                 the source database to be cloned" << std::endl;
	std::cerr << "--dst_db arg             the destination database" << std::endl;
	std::cerr << "--coll arg               the source collection to be cloned" << std::endl;
	std::cerr << "--dst_coll arg           the destination collection" << std::endl;
	std::cerr << "--oplog                  whether to sync oplog" << std::endl;
	std::cerr << "--raw_oplog              whether to only clone oplog" << std::endl;
	std::cerr << "--op_start arg           the start timestamp to sync oplog" << std::endl;
	std::cerr << "--op_end arg             the start timestamp to sync oplog" << std::endl;
	std::cerr << "--dst_op_ns arg          the destination namespace for raw oplog mode" << std::endl;
	std::cerr << "--no_index               whether to clone the db or collection corresponding index" << std::endl;
	std::cerr << "--filter arg             the bson format string used to filter the records to be transfered" << std::endl;
	std::cerr << "--use_mcr                force use MONGODB-CR password machenism" << std::endl;
}

void ParseOptions(int argc, char** argv, Options* opt) {
	int32_t idx = 1;
	std::string time_str;
	int32_t commas_pos;
	while (idx < argc) {
		if (strcasecmp(argv[idx], "--help") == 0) {
			Usage();
			exit(0);
		} else if (strcasecmp(argv[idx], "--src_srv") == 0) {
			opt->src_ip_port = argv[++idx];
		} else if (strcasecmp(argv[idx], "--src_user") == 0) {
			opt->src_user = argv[++idx];
		} else if (strcasecmp(argv[idx], "--src_passwd") == 0) {
			opt->src_passwd = argv[++idx];
		} else if (strcasecmp(argv[idx], "--src_auth_db") == 0) {
			opt->src_auth_db = argv[++idx];
		} else if (strcasecmp(argv[idx], "--dst_srv") == 0) {
			opt->dst_ip_port = argv[++idx];
		} else if (strcasecmp(argv[idx], "--dst_user") == 0) {
			opt->dst_user = argv[++idx];
		} else if (strcasecmp(argv[idx], "--dst_passwd") == 0) {
			opt->dst_passwd = argv[++idx];
		} else if (strcasecmp(argv[idx], "--dst_auth_db") == 0) {
			opt->dst_auth_db = argv[++idx];
		} else if (strcasecmp(argv[idx], "--dst_srv") == 0) {
			opt->dst_ip_port = argv[++idx];
		} else if (strcasecmp(argv[idx], "--db") == 0) {
			opt->db = argv[++idx];
		} else if (strcasecmp(argv[idx], "--dst_db") == 0) {
			opt->dst_db = argv[++idx];
		} else if (strcasecmp(argv[idx], "--coll") == 0) {
			opt->coll = argv[++idx];
		} else if (strcasecmp(argv[idx], "--dst_coll") == 0) {
			opt->dst_coll = argv[++idx];
		} else if (strcasecmp(argv[idx], "--oplog") == 0) {
			opt->oplog = true;
		} else if (strcasecmp(argv[idx], "--raw_oplog") == 0) {
			opt->raw_oplog = true;
		} else if (strcasecmp(argv[idx], "--op_start") == 0) {
			time_str = argv[++idx];
			commas_pos = time_str.find(",");
			opt->oplog_start.sec = atoi(time_str.substr(0, commas_pos).c_str());
			opt->oplog_start.no = atoi(time_str.substr(commas_pos+1).c_str());
		} else if (strcasecmp(argv[idx], "--op_end") == 0) {
			time_str = argv[++idx];
			commas_pos = time_str.find(",");
			opt->oplog_end.sec = atoi(time_str.substr(0, commas_pos).c_str());
			opt->oplog_end.no = atoi(time_str.substr(commas_pos+1).c_str());
		} else if (strcasecmp(argv[idx], "--dst_op_ns") == 0) {
			opt->dst_oplog_ns = argv[++idx];
		} else if (strcasecmp(argv[idx], "--no_index") == 0) {
			opt->no_index = true;
		} else if (strcasecmp(argv[idx], "--filter") == 0) {
			opt->filter = mongo::Query(argv[++idx]);
		} else if (strcasecmp(argv[idx], "--use_mcr") == 0) {
			opt->use_mcr = true;
		} else {
			std::cerr << "Unkown options" << std::endl;
			Usage();
			exit(-1);
		}
		++idx;
	}
}

MongoSync::MongoSync(const Options& opt) 
	: opt_(opt),
	src_conn_(NULL),
	dst_conn_(NULL) {	
}

MongoSync::~MongoSync() {
	if (src_conn_) {
		delete src_conn_;
	}
	if (dst_conn_) {
		delete dst_conn_;
	}
}

MongoSync* MongoSync::NewMongoSync(const Options& opt) {
	MongoSync* mongosync = new MongoSync(opt);
	if (mongosync->InitConn() == -1) {
		delete mongosync;
		return NULL;
	}
	return mongosync;
}

int32_t MongoSync::InitConn() {
	if (!(src_conn_ = ConnectAndAuth(opt_.src_ip_port, opt_.src_auth_db, opt_.src_user, opt_.src_passwd, opt_.use_mcr))
			|| !(dst_conn_ = ConnectAndAuth(opt_.dst_ip_port, opt_.dst_auth_db, opt_.dst_user, opt_.dst_passwd, opt_.use_mcr))) {
		return -1;	
	}
	src_version_ = GetMongoVersion(src_conn_);
	dst_version_ = GetMongoVersion(dst_conn_);
	return 0;
}

mongo::DBClientConnection* MongoSync::ConnectAndAuth(std::string srv_ip_port, std::string auth_db, std::string user, std::string passwd, bool use_mcr) {
	std::string errmsg;
	mongo::DBClientConnection* conn = NULL;
	conn = new mongo::DBClientConnection();	
	if (!conn->connect(srv_ip_port, errmsg)) {
		std::cerr << "connect to srv: " << srv_ip_port << " failed, with errmsg: " << errmsg << std::endl;
		delete conn;
		return NULL;
	}	std::cerr << "connect to srv_rsv: " << srv_ip_port << " ok!" << std::endl;
	if (!passwd.empty()) {
		if (!conn->auth(auth_db, user, passwd, errmsg, use_mcr)) {
			std::cerr << "srv: " << srv_ip_port << ", dbname: " << auth_db << " failed" << std::endl; 
			delete conn;
			return NULL;
		}
		std::cerr << "srv: " << srv_ip_port << ", dbname: " << auth_db << " ok!" << std::endl;
	}
	return conn;
}

void MongoSync::Process() {
	oplog_begin_ = opt_.oplog_start;
	if (opt_.oplog_start.empty()) {
		oplog_begin_ = GetSideOplogTime(src_conn_, oplog_ns_, opt_.db, opt_.coll, true);
	}
	oplog_finish_ = opt_.oplog_end;

	if (need_clone_oplog()) {
		CloneOplog();
		return;
	}
	if ((need_clone_db() || need_clone_coll()) && opt_.oplog_start.empty()) {
		oplog_begin_ = GetSideOplogTime(src_conn_, oplog_ns_, opt_.db, opt_.coll, false);
	}
	if (need_clone_db()) {
		CloneDb();
	} else if (need_clone_coll()) {
		std::string sns = opt_.db + "." + opt_.coll;
		std::string dns = (opt_.dst_db.empty() ? opt_.db : opt_.dst_db) + "." + (opt_.dst_coll.empty() ? opt_.coll : opt_.dst_coll);
		CloneColl(sns, dns);
	}
	if (need_sync_oplog()) {
		SyncOplog();
	}
}

void MongoSync::CloneOplog() {
	GenericProcessOplog(kClone);
}

void MongoSync::SyncOplog() {
	GenericProcessOplog(kApply);		
}

void MongoSync::GenericProcessOplog(OplogProcessOp op) {
	mongo::Query query;	
	if (!opt_.db.empty() && opt_.coll.empty()) {
		query = mongo::Query(BSON("ns" << BSON("$regex" << ("^"+opt_.db)) << "ts" << mongo::GTE << oplog_begin_.timestamp() << mongo::LTE << oplog_finish_.timestamp())); //TODO: this cannot exact out the opt_.db related oplog, but the opt_.db-prefixed related oplog
	} else if (!opt_.db.empty() && !opt_.coll.empty()) {
		NamespaceString ns(opt_.db, opt_.coll);
		query = mongo::Query(BSON("$or" << BSON_ARRAY(BSON("ns" << ns.ns()) << BSON("ns" << ns.db() + ".system.indexes") << BSON("ns" << ns.db() + ".system.$cmd"))
					<< "ts" << mongo::GTE << oplog_begin_.timestamp() << mongo::LTE << oplog_finish_.timestamp()));
	}
	std::auto_ptr<mongo::DBClientCursor> cursor = src_conn_->query(oplog_ns_, query, 0, 0, NULL, mongo::QueryOption_CursorTailable | mongo::QueryOption_AwaitData);
	std::string dst_db, dst_coll;
	if (need_clone_oplog()) {
		NamespaceString ns(opt_.dst_oplog_ns);
		dst_db = ns.db(),
					 dst_coll = ns.coll(); 
	} else {
		dst_db = opt_.dst_db;
		dst_coll = opt_.dst_coll; 
	}
	mongo::BSONObj oplog;	
	OplogTime cur_times;
	while (true) {
		while (!cursor->more()) {
			if (!before(cur_times, oplog_finish_)) {
				std::cerr << std::endl;
				return;
			}
			sleep(1);
		}
		oplog = cursor->next();	
		ProcessSingleOplog(opt_.db, opt_.coll, dst_db, dst_coll, oplog.getOwned(), op);
		memcpy(&cur_times, oplog["ts"].value(), 2*sizeof(int32_t));	
		std::cerr << "\rProgress sync to timestamp: " << cur_times.sec << "," << cur_times.no << "       ";
//			if (!before(cur_times, oplog_finish_)) {
//				break;
//			}
//			if (*reinterpret_cast<uint64_t*>(&oplog_finish_) != static_cast<uint64_t>(-1LL)) {
//				break;	
//			}
	}
	std::cerr << std::endl;

}

void MongoSync::CloneDb() {
	std::vector<std::string> colls;		
	if (GetAllCollByVersion(src_conn_, src_version_, opt_.db, colls) == -1) { // get collections failed
		return;
	}
	std::string dst_db = opt_.dst_db.empty() ? opt_.db : opt_.dst_db;
	for (std::vector<std::string>::const_iterator iter = colls.begin();
			iter != colls.end();
			++iter) {
		CloneColl(opt_.db + "." + *iter, dst_db + "." + *iter);
	}
}

void MongoSync::CloneColl(std::string src_ns, std::string dst_ns, int batch_size) {
//	std::string src_ns = opt_.db + "." + opt_.coll;
//	std::string dst_ns = (opt_.dst_db.empty() ? opt_.db : opt_.dst_db) + "." + (opt_.dst_coll.empty() ? opt_.coll : opt_.dst_coll);
	std::cerr << "clone "	<< src_ns << std::endl;
	uint64_t total = src_conn_->count(src_ns, opt_.filter, mongo::QueryOption_SlaveOk | mongo::QueryOption_NoCursorTimeout), cnt = 0;
	std::auto_ptr<mongo::DBClientCursor> cursor = src_conn_->query(src_ns, opt_.filter, 0, 0, NULL, mongo::QueryOption_SlaveOk);
	std::vector<mongo::BSONObj> batch;
	int32_t acc_size = 0, percent = 0;
	uint64_t st = time(NULL);
	std::string marks, blanks;
	while (cursor->more()) {
		mongo::BSONObj obj = cursor->next();
		acc_size += obj.objsize();
		batch.push_back(obj.getOwned());
		if (acc_size >= batch_size) {
			dst_conn_->insert(dst_ns, batch, mongo::InsertOption_ContinueOnError, &mongo::WriteConcern::unacknowledged);
			batch.clear();
			acc_size = 0;
		}
		++cnt;
		if (!(cnt & 0x3FF)) {
			percent = cnt * 100 / total;
			marks.assign(percent, '#');
			blanks.assign(105-percent, ' ');
			std::cerr << "\rProgress  " << marks << blanks << percent << "%,  elapsed time: " << time(NULL)-st << "s";	
		}
	}
	if (!batch.empty()) {
		dst_conn_->insert(dst_ns, batch, mongo::InsertOption_ContinueOnError, &mongo::WriteConcern::unacknowledged);
	}
	marks.assign(100, '#');
	blanks.assign(105-100, ' ');
	std::cerr << "\rProgress  " << marks << blanks << 100 << "%,  elapsed time: " << time(NULL)-st << "s" << std::endl;;
	if (!opt_.no_index) {
		CloneCollIndex(src_ns, dst_ns);
	}
}

void MongoSync::CloneCollIndex(std::string sns, std::string dns) {
	std::cerr << "\rcloning " << sns << " indexes" << std::endl;
	mongo::BSONObj indexes;
	if (GetCollIndexesByVersion(src_conn_, src_version_, sns, indexes) == -1) {
		return;
	}
	int32_t indexes_num = indexes.nFields(), idx = 0;
	mongo::BSONElement element;
	while (idx < indexes_num) {
		mongo::BSONObjBuilder builder;
		mongo::BSONObjIterator i(indexes.getObjectField(util::Int2Str(idx++)));
		std::string field_name;
		while (i.more()) {
			element = i.next();
			field_name = element.fieldName();
			if (field_name == "background" || field_name == "ns") {
				continue;
			}
			builder << element;			
		}
		builder << "background" << "true";
		builder << "ns" << dns;
		SetCollIndexesByVersion(dst_conn_, dst_version_, dns, builder.obj());
	}
	std::cerr << "\rclone " << sns << " indexes success" << std::endl;
}

void MongoSync::ProcessSingleOplog(const std::string& db, const std::string& coll, std::string& dst_db, std::string& dst_coll, const mongo::BSONObj& oplog, OplogProcessOp op) {
    std::string oplog_ns = oplog.getStringField("ns");	
	if (!db.empty() && coll.empty()) {
		std::string sns = db + ".";
		if (oplog_ns.size() < sns.size() || oplog_ns.substr(0, sns.size()) != sns) {
			return;
		}
	}
	
	if (mongo::str::endsWith(oplog_ns.c_str(), ".system.indexes")) {
		return;
	}

	std::string dns = dst_db + "." + dst_coll;
	if (op == kClone) {
		dst_conn_->insert(dns, oplog, 0, &mongo::WriteConcern::unacknowledged);
		return;
	}

    if (dst_db.empty()) {
        dst_db = db.empty() ? NamespaceString(oplog_ns).db() : db;
    }
    if (dst_coll.empty()) {
        dst_coll = coll.empty() ? NamespaceString(oplog_ns).coll() : coll;
    }
   
	std::string type = oplog.getStringField("op");
	mongo::BSONObj obj = oplog.getObjectField("o");
	switch(type.at(0)) {
		case 'i':
			ApplyInsertOplog(dst_db, dst_coll, oplog);
			break;
		case 'u':
			dst_conn_->update(dns, oplog.getObjectField("o2"), oplog.getObjectField("o"));
			break;
		case 'd':
			dst_conn_->remove(dns, oplog.getObjectField("o"));
			break;
		case 'n':
			break;
		case 'c':
			ApplyCmdOplog(dst_db, dst_coll, oplog, coll == dst_coll);
	}
}

void MongoSync::ApplyInsertOplog(const std::string& dst_db, const std::string& dst_coll, const mongo::BSONObj& oplog) {
	assert(!dst_db.empty() && !dst_coll.empty());

	mongo::BSONObj obj = oplog.getObjectField("o");
	std::string ns = oplog.getStringField("ns");
	std::string dns = dst_db + "." + dst_coll;
	if (ns.size() < sizeof(".system.indexes")
			|| ns.substr(ns.size()-sizeof(".system.indexes")+1) != ".system.indexes") { //not index-ceating oplog
		dst_conn_->insert(dns, obj, 0, &mongo::WriteConcern::unacknowledged);
		return;
	}
	
	mongo::BSONObjBuilder build;
	mongo::BSONObjIterator iter(obj);
	mongo::BSONElement ele;
	std::string field;
	build << "background" << "true" << "ns" << dns;
	while (iter.more()) {
		ele = iter.next();
		field = ele.fieldName();
		if (field == "background" || field == "ns") {
			continue;
		}
		build.append(ele);
	}
	SetCollIndexesByVersion(dst_conn_, dst_version_, dns, build.obj());	
}

void MongoSync::ApplyCmdOplog(const std::string& dst_db, const std::string& dst_coll, const mongo::BSONObj& oplog, bool same_ns) { //TODO: other cmd oplog could be reconsidered
	mongo::BSONObj obj = oplog.getObjectField("o"), tmp;
	std::string first_field = obj.firstElementFieldName();
	if (first_field != "deleteIndexes" && first_field != "dropIndexes") { // Only care the indexes-related command oplog
		return;
	}

	if (!same_ns) {
		mongo::BSONObjIterator iter(obj);
		mongo::BSONElement ele;
		mongo::BSONObjBuilder build;
		std::string field;
		build << "dropIndexes" << dst_coll;
		while (iter.more()) {
			ele = iter.next();
			field = ele.fieldName();
			if (field == "deleteIndexes" || field == "dropIndexes") {
				continue;
			}
			build.append(ele);
		}
		obj = build.obj();
	}
	if (!dst_conn_->runCommand(dst_db, obj, tmp)) {
		std::cerr << "dropIndexes/deleteIndexes failed, errms: " << tmp << std::endl;
	}
}

OplogTime MongoSync::GetSideOplogTime(mongo::DBClientConnection* conn, std::string oplog_ns, std::string db, std::string coll, bool first_or_last) {
	int32_t order = first_or_last ? 1 : -1;	
	mongo::BSONObj obj;
	if (db.empty() || coll.empty()) {
//		obj = conn->findOne(oplog_ns_, mongo::Query().sort("ts", order), NULL, mongo::QueryOption_SlaveOk);	
		obj = conn->findOne(oplog_ns_, mongo::Query().sort("$natural", order), NULL, mongo::QueryOption_SlaveOk);	
	} else if (!db.empty() && coll.empty()) {
//		obj = conn->findOne(oplog_ns_, mongo::Query(BSON("ns" << BSON("$regex" << db))).sort("ts", order), NULL, mongo::QueryOption_SlaveOk);
		obj = conn->findOne(oplog_ns_, mongo::Query(BSON("ns" << BSON("$regex" << db))).sort("$natural", order), NULL, mongo::QueryOption_SlaveOk);
	} else if (!db.empty() && !coll.empty()) {
		NamespaceString ns(db, coll);
		obj = conn->findOne(oplog_ns_, mongo::Query(BSON("$or" << BSON_ARRAY(BSON("ns" << ns.ns()) 
																																					<< BSON("ns" << ns.db() + ".system.indexes")
//																																					<< BSON("ns" << ns.db() + ".system.cmd")))).sort("ts", order), NULL, mongo::QueryOption_SlaveOk);
																																					<< BSON("ns" << ns.db() + ".system.cmd")))).sort("$natural", order), NULL, mongo::QueryOption_SlaveOk);
	} else {
		std::cerr << "get side oplog time erorr" << std::endl;
		exit(-1);
	}
	return *reinterpret_cast<const OplogTime*>(obj["ts"].value());
}

std::string MongoSync::GetMongoVersion(mongo::DBClientConnection* conn) {
	std::string version;
	mongo::BSONObj build_info;
	bool ok = conn->simpleCommand("admin", &build_info, "buildInfo");
	if (ok && build_info["version"].trueValue()) {
		version = build_info.getStringField("version");
	}
	return version;
}

int MongoSync::GetAllCollByVersion(mongo::DBClientConnection* conn, std::string version, std::string db, std::vector<std::string>& colls) {
	std::string version_header = version.substr(0, 4);
	mongo::BSONObj tmp;
	if (version_header == "3.0." || version_header == "3.2.") {
		mongo::BSONObj array;
		if (!conn->runCommand(db, BSON("listCollections" << 1), tmp)) {
			std::cerr << "get " << db << "'s collections failed" << std::endl;
			return -1;
		}
		array = tmp.getObjectField("cursor").getObjectField("firstBatch");
		int32_t idx = 0;
		while (array.hasField(util::Int2Str(idx))) {
			colls.push_back(array.getObjectField(util::Int2Str(idx++)).getStringField("name"));
		} 
	} else if (version_header == "2.4." || version_header == "2.6.") {
		std::auto_ptr<mongo::DBClientCursor> cursor = conn->query(db + ".system.namespaces", mongo::Query(), 0, 0, NULL, mongo::QueryOption_SlaveOk | mongo::QueryOption_NoCursorTimeout);
		if (cursor.get() == NULL) {
			std::cerr << "get " << db << "'s collections failed" << std::endl;
			return -1;
		}
		std::string coll;
		while (cursor->more()) {
			tmp = cursor->next();
			coll = tmp.getStringField("name");
			if (mongoutils::str::endsWith(coll.c_str(), ".system.namespaces") 
					|| mongoutils::str::endsWith(coll.c_str(), ".system.users") 
					|| mongoutils::str::endsWith(coll.c_str(), ".system.indexes")
          || coll.substr(coll.rfind("."), 2) == ".$") {
				continue;
			}
			colls.push_back(coll.substr(coll.find(".")+1));
		}	
	} else {
		std::cerr << version << " is not supported" << std::endl;
		exit(-1);
	}
	return 0;
}

int MongoSync::GetCollIndexesByVersion(mongo::DBClientConnection* conn, std::string version, std::string coll_full_name, mongo::BSONObj& indexes) {
	NamespaceString ns(coll_full_name);
	mongo::BSONObj tmp;
	std::string version_header = version.substr(0, 4);
	if (version_header == "3.0." || version_header == "3.2.") {
		if (!conn->runCommand(ns.db(), BSON("listIndexes" << ns.coll()), tmp)) {
			std::cerr << coll_full_name << " get indexes failed" << std::endl;
			return -1;
		}
		indexes = tmp.getObjectField("cursor").getObjectField("firstBatch");
	} else if (version_header == "2.4." || version == "2.6.") {
		std::auto_ptr<mongo::DBClientCursor> cursor;
		mongo::BSONArrayBuilder array_builder;
		cursor = conn->query(ns.db() + ".system.indexes",mongo::Query(BSON("ns" << coll_full_name)), 0, 0, 0, mongo::QueryOption_SlaveOk | mongo::QueryOption_NoCursorTimeout);
		if (cursor.get() == NULL) {
			std::cerr << coll_full_name << " get indexes failed" << std::endl;
			return -1;
		}	
		while (cursor->more()) {
			tmp = cursor->next();
			array_builder << tmp;	
		}
		indexes = array_builder.arr();
	} else {
		std::cerr << "version: " << version << " is not surpported" << std::endl;
		exit(-1);
	}
	return 0;
}

void MongoSync::SetCollIndexesByVersion(mongo::DBClientConnection* conn, std::string version, std::string coll_full_name, mongo::BSONObj index) {
	std::string version_header = version.substr(0, 4);
	NamespaceString ns(coll_full_name);
	if (version_header == "3.0." || version_header == "3.2.") {
		mongo::BSONObj tmp;
		conn->runCommand(ns.db(), BSON("createIndexes" << ns.coll() << "indexes" << BSON_ARRAY(index)), tmp);
	} else if (version_header == "2.4." || version_header == "2.6.") {
		conn->insert(ns.db() + ".system.indexes", index, 0, &mongo::WriteConcern::unacknowledged);	
	} else {
		std::cerr << "version: " << version << " is not surpported" << std::endl;
		exit(-1);
	}
}

const std::string MongoSync::oplog_ns_ = "local.oplog.rs";
