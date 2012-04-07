// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/net/sqlite_origin_bound_cert_store.h"

#include <list>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/string_util.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/diagnostics/sqlite_diagnostics.h"
#include "content/public/browser/browser_thread.h"
#include "net/base/ssl_client_cert_type.h"
#include "net/base/x509_certificate.h"
#include "sql/meta_table.h"
#include "sql/statement.h"
#include "sql/transaction.h"

using content::BrowserThread;

// This class is designed to be shared between any calling threads and the
// database thread.  It batches operations and commits them on a timer.
class SQLiteOriginBoundCertStore::Backend
    : public base::RefCountedThreadSafe<SQLiteOriginBoundCertStore::Backend> {
 public:
  explicit Backend(const FilePath& path)
      : path_(path),
        db_(NULL),
        num_pending_(0),
        clear_local_state_on_exit_(false) {
  }

  // Creates or load the SQLite database.
  bool Load(
      std::vector<net::DefaultOriginBoundCertStore::OriginBoundCert*>* certs);

  // Batch an origin bound cert addition.
  void AddOriginBoundCert(
      const net::DefaultOriginBoundCertStore::OriginBoundCert& cert);

  // Batch an origin bound cert deletion.
  void DeleteOriginBoundCert(
      const net::DefaultOriginBoundCertStore::OriginBoundCert& cert);

  // Commit pending operations as soon as possible.
  void Flush(const base::Closure& completion_task);

  // Commit any pending operations and close the database.  This must be called
  // before the object is destructed.
  void Close();

  void SetClearLocalStateOnExit(bool clear_local_state);

 private:
  friend class base::RefCountedThreadSafe<SQLiteOriginBoundCertStore::Backend>;

  // You should call Close() before destructing this object.
  ~Backend() {
    DCHECK(!db_.get()) << "Close should have already been called.";
    DCHECK(num_pending_ == 0 && pending_.empty());
  }

  // Database upgrade statements.
  bool EnsureDatabaseVersion();

  class PendingOperation {
   public:
    typedef enum {
      CERT_ADD,
      CERT_DELETE
    } OperationType;

    PendingOperation(
        OperationType op,
        const net::DefaultOriginBoundCertStore::OriginBoundCert& cert)
        : op_(op), cert_(cert) {}

    OperationType op() const { return op_; }
    const net::DefaultOriginBoundCertStore::OriginBoundCert& cert() const {
        return cert_;
    }

   private:
    OperationType op_;
    net::DefaultOriginBoundCertStore::OriginBoundCert cert_;
  };

 private:
  // Batch an origin bound cert operation (add or delete)
  void BatchOperation(
      PendingOperation::OperationType op,
      const net::DefaultOriginBoundCertStore::OriginBoundCert& cert);
  // Commit our pending operations to the database.
  void Commit();
  // Close() executed on the background thread.
  void InternalBackgroundClose();

  FilePath path_;
  scoped_ptr<sql::Connection> db_;
  sql::MetaTable meta_table_;

  typedef std::list<PendingOperation*> PendingOperationsList;
  PendingOperationsList pending_;
  PendingOperationsList::size_type num_pending_;
  // True if the persistent store should be deleted upon destruction.
  bool clear_local_state_on_exit_;
  // Guard |pending_|, |num_pending_| and |clear_local_state_on_exit_|.
  base::Lock lock_;

  DISALLOW_COPY_AND_ASSIGN(Backend);
};

// Version number of the database.
static const int kCurrentVersionNumber = 4;
static const int kCompatibleVersionNumber = 1;

namespace {

// Initializes the certs table, returning true on success.
bool InitTable(sql::Connection* db) {
  if (!db->DoesTableExist("origin_bound_certs")) {
    if (!db->Execute("CREATE TABLE origin_bound_certs ("
                     "origin TEXT NOT NULL UNIQUE PRIMARY KEY,"
                     "private_key BLOB NOT NULL,"
                     "cert BLOB NOT NULL,"
                     "cert_type INTEGER,"
                     "expiration_time INTEGER,"
                     "creation_time INTEGER)"))
      return false;
  }

  return true;
}

}  // namespace

bool SQLiteOriginBoundCertStore::Backend::Load(
    std::vector<net::DefaultOriginBoundCertStore::OriginBoundCert*>* certs) {
  // This function should be called only once per instance.
  DCHECK(!db_.get());

  // Ensure the parent directory for storing certs is created before reading
  // from it.  We make an exception to allow IO on the UI thread here because
  // we are going to disk anyway in db_->Open.  (This code will be moved to the
  // DB thread as part of http://crbug.com/52909.)
  {
    base::ThreadRestrictions::ScopedAllowIO allow_io;
    const FilePath dir = path_.DirName();
    if (!file_util::PathExists(dir) && !file_util::CreateDirectory(dir))
      return false;
  }

  db_.reset(new sql::Connection);
  if (!db_->Open(path_)) {
    NOTREACHED() << "Unable to open cert DB.";
    db_.reset();
    return false;
  }

  if (!EnsureDatabaseVersion() || !InitTable(db_.get())) {
    NOTREACHED() << "Unable to open cert DB.";
    db_.reset();
    return false;
  }

  db_->Preload();

  // Slurp all the certs into the out-vector.
  sql::Statement smt(db_->GetUniqueStatement(
      "SELECT origin, private_key, cert, cert_type, expiration_time, "
      "creation_time FROM origin_bound_certs"));
  if (!smt) {
    NOTREACHED() << "select statement prep failed";
    db_.reset();
    return false;
  }

  while (smt.Step()) {
    std::string private_key_from_db, cert_from_db;
    smt.ColumnBlobAsString(1, &private_key_from_db);
    smt.ColumnBlobAsString(2, &cert_from_db);
    scoped_ptr<net::DefaultOriginBoundCertStore::OriginBoundCert> cert(
        new net::DefaultOriginBoundCertStore::OriginBoundCert(
            smt.ColumnString(0),  // origin
            static_cast<net::SSLClientCertType>(smt.ColumnInt(3)),
            base::Time::FromInternalValue(smt.ColumnInt64(5)),
            base::Time::FromInternalValue(smt.ColumnInt64(4)),
            private_key_from_db,
            cert_from_db));
    certs->push_back(cert.release());
  }

  return true;
}

bool SQLiteOriginBoundCertStore::Backend::EnsureDatabaseVersion() {
  // Version check.
  if (!meta_table_.Init(
      db_.get(), kCurrentVersionNumber, kCompatibleVersionNumber)) {
    return false;
  }

  if (meta_table_.GetCompatibleVersionNumber() > kCurrentVersionNumber) {
    LOG(WARNING) << "Origin bound cert database is too new.";
    return false;
  }

  int cur_version = meta_table_.GetVersionNumber();
  if (cur_version == 1) {
    sql::Transaction transaction(db_.get());
    if (!transaction.Begin())
      return false;
    if (!db_->Execute("ALTER TABLE origin_bound_certs ADD COLUMN cert_type "
                      "INTEGER")) {
      LOG(WARNING) << "Unable to update origin bound cert database to "
                   << "version 2.";
      return false;
    }
    // All certs in version 1 database are rsa_sign, which has a value of 1.
    if (!db_->Execute("UPDATE origin_bound_certs SET cert_type = 1")) {
      LOG(WARNING) << "Unable to update origin bound cert database to "
                   << "version 2.";
      return false;
    }
    ++cur_version;
    meta_table_.SetVersionNumber(cur_version);
    meta_table_.SetCompatibleVersionNumber(
        std::min(cur_version, kCompatibleVersionNumber));
    transaction.Commit();
  }

  if (cur_version <= 3) {
    sql::Transaction transaction(db_.get());
    if (!transaction.Begin())
      return false;

    if (cur_version == 2) {
      if (!db_->Execute("ALTER TABLE origin_bound_certs ADD COLUMN "
                        "expiration_time INTEGER")) {
        LOG(WARNING) << "Unable to update origin bound cert database to "
                     << "version 4.";
        return false;
      }
    }

    if (!db_->Execute("ALTER TABLE origin_bound_certs ADD COLUMN "
                      "creation_time INTEGER")) {
      LOG(WARNING) << "Unable to update origin bound cert database to "
                   << "version 4.";
      return false;
    }

    sql::Statement smt(db_->GetUniqueStatement(
        "SELECT origin, cert FROM origin_bound_certs"));
    sql::Statement update_expires_smt(db_->GetUniqueStatement(
        "UPDATE origin_bound_certs SET expiration_time = ? WHERE origin = ?"));
    sql::Statement update_creation_smt(db_->GetUniqueStatement(
        "UPDATE origin_bound_certs SET creation_time = ? WHERE origin = ?"));
    if (!smt || !update_expires_smt || !update_creation_smt) {
      LOG(WARNING) << "Unable to update origin bound cert database to "
                   << "version 4.";
      return false;
    }
    while (smt.Step()) {
      std::string origin = smt.ColumnString(0);
      std::string cert_from_db;
      smt.ColumnBlobAsString(1, &cert_from_db);
      // Parse the cert and extract the real value and then update the DB.
      scoped_refptr<net::X509Certificate> cert(
          net::X509Certificate::CreateFromBytes(
              cert_from_db.data(), cert_from_db.size()));
      if (cert) {
        if (cur_version == 2) {
          update_expires_smt.Reset();
          update_expires_smt.BindInt64(0,
                                       cert->valid_expiry().ToInternalValue());
          update_expires_smt.BindString(1, origin);
          if (!update_expires_smt.Run()) {
            LOG(WARNING) << "Unable to update origin bound cert database to "
                         << "version 4.";
            return false;
          }
        }

        update_creation_smt.Reset();
        update_creation_smt.BindInt64(0, cert->valid_start().ToInternalValue());
        update_creation_smt.BindString(1, origin);
        if (!update_creation_smt.Run()) {
          LOG(WARNING) << "Unable to update origin bound cert database to "
                       << "version 4.";
          return false;
        }
      } else {
        // If there's a cert we can't parse, just leave it.  It'll get replaced
        // with a new one if we ever try to use it.
        LOG(WARNING) << "Error parsing cert for database upgrade for origin "
                     << smt.ColumnString(0);
      }
    }

    cur_version = 4;
    meta_table_.SetVersionNumber(cur_version);
    meta_table_.SetCompatibleVersionNumber(
        std::min(cur_version, kCompatibleVersionNumber));
    transaction.Commit();
  }

  // Put future migration cases here.

  // When the version is too old, we just try to continue anyway, there should
  // not be a released product that makes a database too old for us to handle.
  LOG_IF(WARNING, cur_version < kCurrentVersionNumber) <<
      "Origin bound cert database version " << cur_version <<
      " is too old to handle.";

  return true;
}

void SQLiteOriginBoundCertStore::Backend::AddOriginBoundCert(
    const net::DefaultOriginBoundCertStore::OriginBoundCert& cert) {
  BatchOperation(PendingOperation::CERT_ADD, cert);
}

void SQLiteOriginBoundCertStore::Backend::DeleteOriginBoundCert(
    const net::DefaultOriginBoundCertStore::OriginBoundCert& cert) {
  BatchOperation(PendingOperation::CERT_DELETE, cert);
}

void SQLiteOriginBoundCertStore::Backend::BatchOperation(
    PendingOperation::OperationType op,
    const net::DefaultOriginBoundCertStore::OriginBoundCert& cert) {
  // Commit every 30 seconds.
  static const int kCommitIntervalMs = 30 * 1000;
  // Commit right away if we have more than 512 outstanding operations.
  static const size_t kCommitAfterBatchSize = 512;
  DCHECK(!BrowserThread::CurrentlyOn(BrowserThread::DB));

  // We do a full copy of the cert here, and hopefully just here.
  scoped_ptr<PendingOperation> po(new PendingOperation(op, cert));

  PendingOperationsList::size_type num_pending;
  {
    base::AutoLock locked(lock_);
    pending_.push_back(po.release());
    num_pending = ++num_pending_;
  }

  if (num_pending == 1) {
    // We've gotten our first entry for this batch, fire off the timer.
    BrowserThread::PostDelayedTask(
        BrowserThread::DB, FROM_HERE,
        base::Bind(&Backend::Commit, this), kCommitIntervalMs);
  } else if (num_pending == kCommitAfterBatchSize) {
    // We've reached a big enough batch, fire off a commit now.
    BrowserThread::PostTask(
        BrowserThread::DB, FROM_HERE,
        base::Bind(&Backend::Commit, this));
  }
}

void SQLiteOriginBoundCertStore::Backend::Commit() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));

  PendingOperationsList ops;
  {
    base::AutoLock locked(lock_);
    pending_.swap(ops);
    num_pending_ = 0;
  }

  // Maybe an old timer fired or we are already Close()'ed.
  if (!db_.get() || ops.empty())
    return;

  sql::Statement add_smt(db_->GetCachedStatement(SQL_FROM_HERE,
      "INSERT INTO origin_bound_certs (origin, private_key, cert, cert_type, "
      "expiration_time) VALUES (?,?,?,?,?)"));
  if (!add_smt) {
    NOTREACHED();
    return;
  }

  sql::Statement del_smt(db_->GetCachedStatement(SQL_FROM_HERE,
                             "DELETE FROM origin_bound_certs WHERE origin=?"));
  if (!del_smt) {
    NOTREACHED();
    return;
  }

  sql::Transaction transaction(db_.get());
  if (!transaction.Begin()) {
    NOTREACHED();
    return;
  }
  for (PendingOperationsList::iterator it = ops.begin();
       it != ops.end(); ++it) {
    // Free the certs as we commit them to the database.
    scoped_ptr<PendingOperation> po(*it);
    switch (po->op()) {
      case PendingOperation::CERT_ADD: {
        add_smt.Reset();
        add_smt.BindString(0, po->cert().origin());
        const std::string& private_key = po->cert().private_key();
        add_smt.BindBlob(1, private_key.data(), private_key.size());
        const std::string& cert = po->cert().cert();
        add_smt.BindBlob(2, cert.data(), cert.size());
        add_smt.BindInt(3, po->cert().type());
        add_smt.BindInt64(4, po->cert().expiration_time().ToInternalValue());
        if (!add_smt.Run())
          NOTREACHED() << "Could not add an origin bound cert to the DB.";
        break;
      }
      case PendingOperation::CERT_DELETE:
        del_smt.Reset();
        del_smt.BindString(0, po->cert().origin());
        if (!del_smt.Run())
          NOTREACHED() << "Could not delete an origin bound cert from the DB.";
        break;

      default:
        NOTREACHED();
        break;
    }
  }
  transaction.Commit();
}

void SQLiteOriginBoundCertStore::Backend::Flush(
    const base::Closure& completion_task) {
  DCHECK(!BrowserThread::CurrentlyOn(BrowserThread::DB));
  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE, base::Bind(&Backend::Commit, this));
  if (!completion_task.is_null()) {
    // We want the completion task to run immediately after Commit() returns.
    // Posting it from here means there is less chance of another task getting
    // onto the message queue first, than if we posted it from Commit() itself.
    BrowserThread::PostTask(BrowserThread::DB, FROM_HERE, completion_task);
  }
}

// Fire off a close message to the background thread. We could still have a
// pending commit timer that will be holding a reference on us, but if/when
// this fires we will already have been cleaned up and it will be ignored.
void SQLiteOriginBoundCertStore::Backend::Close() {
  DCHECK(!BrowserThread::CurrentlyOn(BrowserThread::DB));
  // Must close the backend on the background thread.
  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(&Backend::InternalBackgroundClose, this));
}

void SQLiteOriginBoundCertStore::Backend::InternalBackgroundClose() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
  // Commit any pending operations
  Commit();

  db_.reset();

  if (clear_local_state_on_exit_)
    file_util::Delete(path_, false);
}

void SQLiteOriginBoundCertStore::Backend::SetClearLocalStateOnExit(
    bool clear_local_state) {
  base::AutoLock locked(lock_);
  clear_local_state_on_exit_ = clear_local_state;
}

SQLiteOriginBoundCertStore::SQLiteOriginBoundCertStore(const FilePath& path)
    : backend_(new Backend(path)) {
}

SQLiteOriginBoundCertStore::~SQLiteOriginBoundCertStore() {
  if (backend_.get()) {
    backend_->Close();
    // Release our reference, it will probably still have a reference if the
    // background thread has not run Close() yet.
    backend_ = NULL;
  }
}

bool SQLiteOriginBoundCertStore::Load(
    std::vector<net::DefaultOriginBoundCertStore::OriginBoundCert*>* certs) {
  return backend_->Load(certs);
}

void SQLiteOriginBoundCertStore::AddOriginBoundCert(
    const net::DefaultOriginBoundCertStore::OriginBoundCert& cert) {
  if (backend_.get())
    backend_->AddOriginBoundCert(cert);
}

void SQLiteOriginBoundCertStore::DeleteOriginBoundCert(
    const net::DefaultOriginBoundCertStore::OriginBoundCert& cert) {
  if (backend_.get())
    backend_->DeleteOriginBoundCert(cert);
}

void SQLiteOriginBoundCertStore::SetClearLocalStateOnExit(
    bool clear_local_state) {
  if (backend_.get())
    backend_->SetClearLocalStateOnExit(clear_local_state);
}

void SQLiteOriginBoundCertStore::Flush(const base::Closure& completion_task) {
  if (backend_.get())
    backend_->Flush(completion_task);
  else if (!completion_task.is_null())
    MessageLoop::current()->PostTask(FROM_HERE, completion_task);
}
