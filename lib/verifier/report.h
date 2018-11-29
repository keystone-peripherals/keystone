//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#ifndef _REPORT_H__
#define _REPORT_H__

#include <iostream>
#include "sha3/sha3.h"
#include "ed25519/ed25519.h"
#include "json11.h"
#include "keys.h"


struct enclave_report_t
{
  byte hash[MDSIZE];
  uint64_t data_len;
  byte data[ATTEST_DATA_MAXLEN];
  byte signature[SIGNATURE_SIZE];
};

struct sm_report_t
{
  byte hash[MDSIZE];
  byte public_key[PUBLIC_KEY_SIZE];
  byte signature[SIGNATURE_SIZE];
};

struct report_t
{
  struct enclave_report_t enclave;
  struct sm_report_t sm;
  byte dev_public_key[PUBLIC_KEY_SIZE];
};

class Report
{
  private:
    struct report_t report;
  public:
    std::string BytesToHex(byte* bytes, size_t len);
    void HexToBytes(byte* bytes, size_t len, std::string hexstr);
    void fromJson(std::string json);
    void fromBytes(byte* bin);
    std::string stringfy();
    void printJson();
    int verify(void* data, size_t datalen);
    int verify();
    void* getDataSection();
    size_t getDataSize();
};

#endif
