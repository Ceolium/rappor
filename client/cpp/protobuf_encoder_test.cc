// Copyright 2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cassert>  // assert
#include <cstdlib>  // strtol, strtof
#include <iostream>
#include <stdio.h>
#include <vector>

#include "protobuf_encoder.h"
#include "rappor.pb.h"
#include "libc_rand_impl.h"
#include "openssl_hash_impl.h"

// Like atoi, but with basic (not exhaustive) error checking.
bool StringToInt(const char* s, int* result) {
  bool ok = true;
  char* end;  // mutated by strtol

  *result = strtol(s, &end, 10);  // base 10
  // If strol didn't consume any characters, it failed.
  if (end == s) {
    ok = false;
  }
  return ok;
}

// Like atof, but with basic (not exhaustive) error checking.
bool StringToFloat(const char* s, float* result) {
  bool ok = true;
  char* end;  // mutated by strtof

  *result = strtof(s, &end);
  // If strof didn't consume any characters, it failed.
  if (end == s) {
    ok = false;
  }
  return ok;
}

// Copy a report into a string, which can go in a protobuf.
void BitsToString(rappor::Bits b, std::string* output, int num_bytes) {
  output->assign(num_bytes, '\0');
  for (int i = 0; i < num_bytes; ++i) {
    // "little endian" string
    (*output)[i] = b & 0xFF;  // last byte
    b >>= 8;
  }
}

// Print a report, with the most significant bit first.
void PrintBitString(const std::string& s) {
  for (int i = s.size() - 1; i >= 0; --i) {
    unsigned char byte = s[i];
    for (int j = 7; j >= 0; --j) {
      bool bit = byte & (1 << j);
      std::cout << (bit ? "1" : "0");
    }
  }
}

// Global constants
const rappor::Params kParams4 = {
  .num_bits = 16, .num_hashes = 2, .num_cohorts = 128,
  .prob_f = 0.25f, .prob_p = 0.5f, .prob_q = 0.75f
};

int main(int argc, char** argv) {
  if (argc != 7) {
    rappor::log(
        "Usage: protobuf_encoder_test <num bits> <num hashes> <num cohorts> "
        "p q f");
    exit(1);
  }

  rappor::Params params;

  rappor::Params params2 = { 16, 2, 128, 0.25f, 0.5f, 0.75f };

  // new initializer style
  rappor::Params params3 = {
    .num_bits = 16, .num_hashes = 2, .num_cohorts = 128,
    .prob_f = 0.25f, .prob_p = 0.5f, .prob_q = 0.75f
  };


  bool ok1 = StringToInt(argv[1], &params.num_bits);
  bool ok2 = StringToInt(argv[2], &params.num_hashes);
  bool ok3 = StringToInt(argv[3], &params.num_cohorts);

  bool ok4 = StringToFloat(argv[4], &params.prob_p);
  bool ok5 = StringToFloat(argv[5], &params.prob_q);
  bool ok6 = StringToFloat(argv[6], &params.prob_f);

  if (!ok1) {
    rappor::log("Invalid number of bits: '%s'", argv[1]);
    exit(1);
  }
  if (!ok2) {
    rappor::log("Invalid number of hashes: '%s'", argv[2]);
    exit(1);
  }
  if (!ok3) {
    rappor::log("Invalid number of cohorts: '%s'", argv[3]);
    exit(1);
  }
  if (!ok4) {
    rappor::log("Invalid float p: '%s'", argv[4]);
    exit(1);
  }
  if (!ok5) {
    rappor::log("Invalid float q: '%s'", argv[5]);
    exit(1);
  }
  if (!ok6) {
    rappor::log("Invalid float f: '%s'", argv[6]);
    exit(1);
  }

  rappor::log("bits %d / hashes %d / cohorts %d", params.num_bits,
      params.num_hashes, params.num_cohorts);
  rappor::log("p %f / q %f / f %f", params.prob_p, params.prob_q,
      params.prob_f);

  int num_bytes = params.num_bits / 8;

  // TODO: Add flag for
  // - num_clients
  // - -r libc / kernel
  // - -c openssl / nacl crpto

  rappor::LibcRandGlobalInit();  // seed
  rappor::LibcRand libc_rand(params.num_bits, params.prob_p, params.prob_q);

  /*
  // TODO: Create an encoder for each client
  std::vector<rappor::Encoder*> encoders(num_cohorts);

  for (int i = 0; i < num_cohorts; ++i) {
    encoders[i] = new rappor::Encoder(
        i, num_bits, num_hashes,
        0.50, rappor::Md5, rappor::Hmac, client_secret, 
        libc_rand);

    assert(encoders[i]->IsValid());  // bad instantiation
  }
  */

  // maybe have rappor_encode and rappor_demo
  // demo shows how to encode multiple metrics

  std::string line;

  // CSV header
  std::cout << "client,cohort,bloom,prr,rappor\n";

  // Consume header line
  std::getline(std::cin, line);
  if (line != "client,cohort,value") {
    rappor::log("Expected CSV header 'client,cohort,value'");
    return 1;
  }

  rappor::log("HI");

  rappor::ReportList report_list;
  // TODO: ReportList for each client, and assign the correct cohort.
  report_list.set_cohort(99);

  while (true) {
    std::getline(std::cin, line);  // no trailing newline
    //rappor::log("Got line %s", line.c_str());

    if (line.empty()) {
      break;  // EOF
    }

    size_t comma1_pos = line.find(',');
    if (comma1_pos == std::string::npos) {
      rappor::log("Expected , in line '%s'", line.c_str());
      return 1;
    }
    size_t comma2_pos = line.find(',', comma1_pos + 1);
    if (comma2_pos == std::string::npos) {
      rappor::log("Expected second , in line '%s'", line.c_str());
      return 1;
    }

    // substr(pos, length) not pos, end
    std::string client_str = line.substr(0, comma1_pos);  // everything before comma
    // everything between first and second comma
    std::string cohort_str = line.substr(comma1_pos + 1,
        comma2_pos - comma1_pos);

    std::string value = line.substr(comma2_pos + 1);  // everything after

    int cohort;
    bool cohort_ok = StringToInt(cohort_str.c_str(), &cohort);
    if (!cohort_ok) {
      rappor::log("Invalid cohort number '%s'", cohort_str.c_str());
      return 1;
    }

    rappor::Deps deps(cohort, rappor::Md5, client_str /*client_secret*/,
                      rappor::Hmac, libc_rand);

    // For now, construct a new encoder every time.  We could construct one for
    // each client.
    rappor::Encoder e(params, deps);

    rappor::Bits bloom;
    rappor::Bits prr;
    rappor::Bits irr;
    bool ok = e._EncodeInternal(value, &bloom, &prr, &irr);

    // NOTE: Are there really encoding errors?
    if (!ok) {
      rappor::log("Error encoding string %s", line.c_str());
      break;
    }
    std::string bloom_str;
    BitsToString(bloom, &bloom_str, num_bytes);

    std::string prr_str;
    BitsToString(prr, &prr_str, num_bytes);

    std::string irr_str;
    BitsToString(irr, &irr_str, num_bytes);

    // Output CSV row.

    std::cout << client_str;
    std::cout << ',';
    std::cout << cohort;
    std::cout << ',';
    PrintBitString(bloom_str);
    std::cout << ',';
    PrintBitString(prr_str);
    std::cout << ',';
    PrintBitString(irr_str);

    std::cout << "\n";

    //
    // Test out ProtobufEncoder
    //

    const int kNameField = 1;
    const int kAddressField = 2;

    // Set up schema with two fields.
    rappor::RecordSchema s;
    s.AddString(kNameField, params);
    s.AddString(kAddressField, params);

    // Instantiate encoder.
    rappor::ProtobufEncoder protobuf_encoder(s, deps);

    // Construct a recorder, and then encode it into a new entry in the report
    // list.
    rappor::Record record;
    record.AddString(kNameField, "foo");
    //record.AddBoolean(kAddressField, false);
    record.AddString(kAddressField, "bar");
    //record.AddBoolean(kAddressField, false);

    rappor::Report* report = report_list.add_report();
    if (!protobuf_encoder.Encode(record, report)) {
      rappor::log("Error encoding record %s", line.c_str());
      break;
    }

    rappor::log("----------");

    rappor::Report* report2 = report_list.add_report();
    rappor::StringEncoder string_encoder(kNameField, params, deps);
    if (!string_encoder.EncodeString("STRING", report2)) {
      rappor::log("Error encoding string %s", line.c_str());
      break;
    }

    rappor::log("----------");

    rappor::Report* report3 = report_list.add_report();
    rappor::OrdinalEncoder ordinal_encoder(kNameField, params, deps);
    if (!ordinal_encoder.EncodeOrdinal(10, report3)) {
      rappor::log("Error encoding ordinal %s", line.c_str());
      break;
    }

    rappor::log("----------");

    rappor::log("RecordReport [%s]", report->DebugString().c_str());
  }

  rappor::log("----------");

  rappor::log("report list [%s]", report_list.DebugString().c_str());

  // Cleanup
  /*
  for (int i = 0; i < num_cohorts; ++i) {
    delete encoders[i];
  }
  */
}
