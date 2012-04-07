/* Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <sstream>

#include "native_client/src/debug_server/gdb_rsp/session.h"
#include "native_client/src/debug_server/gdb_rsp/test.h"
#include "native_client/src/debug_server/port/platform.h"

using gdb_rsp::Session;
using gdb_rsp::Packet;

// Transport simulation class, this stores data and a r/w index
// to simulate one direction of a pipe, or a pipe to self.
class SharedVector {
 public:
  SharedVector() : rd(0), wr(0) {}

 public:
  std::vector<char> data;
  volatile uint32_t rd;
  volatile uint32_t wr;
};

// Simulates a transport (such as a socket), the reports "ready"
// when polled, but fails on TX/RX.
class DCSocketTransport : public port::ITransport {
 public:
  virtual int32_t Read(void *ptr, int32_t len) {
    (void) ptr;
    (void) len;
    return -1;
  }

  virtual int32_t Write(const void *ptr, int32_t len) {
    (void) ptr;
    (void) len;
    return -1;
  }

  virtual bool ReadWaitWithTimeout(uint32_t ms) {
    (void) ms;
    return true;
  }

  virtual void Disconnect() {}
  virtual bool DataAvail() { return true; }
};


// Simulate a transport transmitting data Q'd in TX and verifying that
// inbound data matches expected "golden" string.
class GoldenTransport : public port::ITransport {
 public:
  GoldenTransport(const char *rx, const char *tx, int cnt) {
    rx_ = rx;
    tx_ = tx;
    cnt_ = cnt;
    txCnt_ = 0;
    rxCnt_ = 0;
    errs_ = 0;
    disconnected_ = false;
  }

  virtual int32_t Read(void *ptr, int32_t len) {
    if (disconnected_) return -1;
    memcpy(ptr, &rx_[rxCnt_], len);
    rxCnt_ += len;
    if (static_cast<int>(strlen(rx_)) < rxCnt_) {
      printf("End of RX\n");
      errs_++;
    }
    return len;
  }

  //  Read from this link, return a negative value if there is an error
  virtual int32_t Write(const void *ptr, int32_t len) {
    const char *str = reinterpret_cast<const char *>(ptr);
    if (disconnected_) return -1;
    if (strncmp(str, &tx_[txCnt_], len) != 0) {
      printf("TX mismatch in %s vs %s.\n", str, &tx_[txCnt_]);
      errs_++;
    }
    txCnt_ += len;
    return len;
  }

  virtual bool ReadWaitWithTimeout(uint32_t ms) {
    if (disconnected_) return true;

    for (int loop = 0; loop < 8; loop++) {
      if (DataAvail()) return true;
      port::IPlatform::Relinquish(ms >> 3);
    }
    return false;
  }

  virtual void Disconnect() {
    disconnected_ = true;
  }

  virtual bool DataAvail() {
     return rxCnt_ < static_cast<int>(strlen(rx_));
  }

  int errs() { return errs_; }


 protected:
  const char *rx_;
  const char *tx_;
  int cnt_;
  int rxCnt_;
  int txCnt_;
  int errs_;
  bool disconnected_;
};


class TestTransport : public port::ITransport {
 public:
  TestTransport(SharedVector *rvec, SharedVector *wvec) {
    rvector_ = rvec;
    wvector_ = wvec;
    disconnected_ = false;
  }

  virtual int32_t Read(void *ptr, int32_t len) {
    if (disconnected_) return -1;
    DataAvail();

    int max = rvector_->wr - rvector_->rd;
    if (max > len)
      max = len;

    if (max > 0) {
      char *src = &rvector_->data[rvector_->rd];
      memcpy(ptr, src, max);
    }
    rvector_->rd += max;
    return max;
  }

  virtual int32_t Write(const void *ptr, int32_t len) {
    if (disconnected_) return -1;

    wvector_->data.resize(wvector_->wr + len);
    memcpy(&wvector_->data[wvector_->wr], ptr, len);
    wvector_->wr += len;
    return len;
  }

  virtual bool ReadWaitWithTimeout(uint32_t ms) {
    if (disconnected_) return true;

    for (int loop = 0; loop < 8; loop++) {
      if (DataAvail()) return true;
      port::IPlatform::Relinquish(ms >> 3);
    }
    return false;
  }

  virtual void Disconnect() {
    disconnected_ = true;
  }

  //  Return true if vec->data is availible (
  virtual bool DataAvail() {
     return (rvector_->rd < rvector_->wr);
  }

 protected:
  SharedVector *rvector_;
  SharedVector *wvector_;
  bool disconnected_;
};


int TestSession() {
  int errs = 0;
  Packet pktOut;
  Packet pktIn;
  SharedVector vec;

  // Create a "loopback" session by using the same
  // FIFO for ingress and egress.
  Session cli;
  Session srv;

  if (cli.Init(NULL)) {
    printf("Initializing with NULL did not fail.\n");
    errs++;
  }

  cli.Init(new TestTransport(&vec, &vec));
  srv.Init(new TestTransport(&vec, &vec));

  // Check, Set,Clear,Get flags.
  cli.ClearFlags(static_cast<uint32_t>(-1));
  cli.SetFlags(Session::IGNORE_ACK | Session::DEBUG_RECV);
  if (cli.GetFlags() != (Session::IGNORE_ACK + Session::DEBUG_RECV)) {
    printf("SetFlag failed.\n");
    errs++;
  }
  cli.ClearFlags(Session::IGNORE_ACK | Session::DEBUG_SEND);
  if (cli.GetFlags() != Session::DEBUG_RECV) {
    printf("ClearFlag failed.\n");
    errs++;
  }

  // Check Send Packet of known value.
  const char *str = "1234";

  pktOut.AddString(str);
  cli.SendPacketOnly(&pktOut);
  srv.GetPacket(&pktIn);
  std::string out;
  pktIn.GetString(&out);
  if (out != str) {
    printf("Send Only failed.\n");
    errs++;
  }

  // Check send against golden transactions
  const char tx[] = { "$1234#ca+" };
  const char rx[] = { "+$OK#9a" };
  GoldenTransport gold(rx, tx, 2);
  Session uni;
  uni.Init(&gold);

  pktOut.Clear();
  pktOut.AddString(str);
  if (!uni.SendPacket(&pktOut)) {
    printf("Send failed.\n");
    errs++;
  }
  if (!uni.GetPacket(&pktIn)) {
    printf("Get failed.\n");
    errs++;
  }
  pktIn.GetString(&out);
  if (out != "OK") {
    printf("Send/Get failed.\n");
    errs++;
  }

  // Check that a closed Transport reports to session
  if (!uni.Connected()) {
    printf("Expecting uni to be connected.\n");
    errs++;
  }
  gold.Disconnect();
  uni.GetPacket(&pktIn);
  if (uni.Connected()) {
    printf("Expecting uni to be disconnected.\n");
    errs++;
  }

  // Check that a failed read/write reports DC
  DCSocketTransport dctrans;
  Session dctest;
  dctest.Init(&dctrans);
  if (!dctest.Connected()) {
    printf("Expecting dctest to be connected.\n");
    errs++;
  }
  dctest.GetPacket(&pktIn);
  if (dctest.Connected()) {
    printf("Expecting dctest to be disconnected.\n");
    errs++;
  }

  errs += gold.errs();
  return errs;
}

