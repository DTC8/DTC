/*
* Copyright [2021] JD.com, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#pragma once

#if !defined(ROCKSDB_LITE)

#include <string>

#include "env.h"

namespace rocksdb {

class EncryptionProvider;

// Returns an Env that encrypts data when stored on disk and decrypts data when
// read from disk.
Env* NewEncryptedEnv(Env* base_env, EncryptionProvider* provider);

// BlockAccessCipherStream is the base class for any cipher stream that
// supports random access at block level (without requiring data from other
// blocks). E.g. CTR (Counter operation mode) supports this requirement.
class BlockAccessCipherStream {
 public:
  virtual ~BlockAccessCipherStream(){};

  // BlockSize returns the size of each block supported by this cipher stream.
  virtual size_t BlockSize() = 0;

  // Encrypt one or more (partial) blocks of data at the file offset.
  // Length of data is given in dataSize.
  virtual Status Encrypt(uint64_t fileOffset, char* data, size_t dataSize);

  // Decrypt one or more (partial) blocks of data at the file offset.
  // Length of data is given in dataSize.
  virtual Status Decrypt(uint64_t fileOffset, char* data, size_t dataSize);

 protected:
  // Allocate scratch space which is passed to EncryptBlock/DecryptBlock.
  virtual void AllocateScratch(std::string&) = 0;

  // Encrypt a block of data at the given block index.
  // Length of data is equal to BlockSize();
  virtual Status EncryptBlock(uint64_t blockIndex, char* data,
                              char* scratch) = 0;

  // Decrypt a block of data at the given block index.
  // Length of data is equal to BlockSize();
  virtual Status DecryptBlock(uint64_t blockIndex, char* data,
                              char* scratch) = 0;
};

// BlockCipher
class BlockCipher {
 public:
  virtual ~BlockCipher(){};

  // BlockSize returns the size of each block supported by this cipher stream.
  virtual size_t BlockSize() = 0;

  // Encrypt a block of data.
  // Length of data is equal to BlockSize().
  virtual Status Encrypt(char* data) = 0;

  // Decrypt a block of data.
  // Length of data is equal to BlockSize().
  virtual Status Decrypt(char* data) = 0;
};

// Implements a BlockCipher using ROT13.
//
// Note: This is a sample implementation of BlockCipher,
// it is NOT considered safe and should NOT be used in production.
class ROT13BlockCipher : public BlockCipher {
 private:
  size_t blockSize_;

 public:
  ROT13BlockCipher(size_t blockSize) : blockSize_(blockSize) {}
  virtual ~ROT13BlockCipher(){};

  // BlockSize returns the size of each block supported by this cipher stream.
  virtual size_t BlockSize() override { return blockSize_; }

  // Encrypt a block of data.
  // Length of data is equal to BlockSize().
  virtual Status Encrypt(char* data) override;

  // Decrypt a block of data.
  // Length of data is equal to BlockSize().
  virtual Status Decrypt(char* data) override;
};

// CTRCipherStream implements BlockAccessCipherStream using an
// Counter operations mode.
// See https://en.wikipedia.org/wiki/Block_cipher_mode_of_operation
//
// Note: This is a possible implementation of BlockAccessCipherStream,
// it is considered suitable for use.
class CTRCipherStream final : public BlockAccessCipherStream {
 private:
  BlockCipher& cipher_;
  std::string iv_;
  uint64_t initialCounter_;

 public:
  CTRCipherStream(BlockCipher& c, const char* iv, uint64_t initialCounter)
      : cipher_(c), iv_(iv, c.BlockSize()), initialCounter_(initialCounter){};
  virtual ~CTRCipherStream(){};

  // BlockSize returns the size of each block supported by this cipher stream.
  virtual size_t BlockSize() override { return cipher_.BlockSize(); }

 protected:
  // Allocate scratch space which is passed to EncryptBlock/DecryptBlock.
  virtual void AllocateScratch(std::string&) override;

  // Encrypt a block of data at the given block index.
  // Length of data is equal to BlockSize();
  virtual Status EncryptBlock(uint64_t blockIndex, char* data,
                              char* scratch) override;

  // Decrypt a block of data at the given block index.
  // Length of data is equal to BlockSize();
  virtual Status DecryptBlock(uint64_t blockIndex, char* data,
                              char* scratch) override;
};

// The encryption provider is used to create a cipher stream for a specific
// file. The returned cipher stream will be used for actual
// encryption/decryption actions.
class EncryptionProvider {
 public:
  virtual ~EncryptionProvider(){};

  // GetPrefixLength returns the length of the prefix that is added to every
  // file and used for storing encryption options. For optimal performance, the
  // prefix length should be a multiple of the page size.
  virtual size_t GetPrefixLength() = 0;

  // CreateNewPrefix initialized an allocated block of prefix memory
  // for a new file.
  virtual Status CreateNewPrefix(const std::string& fname, char* prefix,
                                 size_t prefixLength) = 0;

  // CreateCipherStream creates a block access cipher stream for a file given
  // given name and options.
  virtual Status CreateCipherStream(
      const std::string& fname, const EnvOptions& options, Slice& prefix,
      std::unique_ptr<BlockAccessCipherStream>* result) = 0;
};

// This encryption provider uses a CTR cipher stream, with a given block cipher
// and IV.
//
// Note: This is a possible implementation of EncryptionProvider,
// it is considered suitable for use, provided a safe BlockCipher is used.
class CTREncryptionProvider : public EncryptionProvider {
 private:
  BlockCipher& cipher_;

 protected:
  const static size_t defaultPrefixLength = 4096;

 public:
  CTREncryptionProvider(BlockCipher& c) : cipher_(c){};
  virtual ~CTREncryptionProvider() {}

  // GetPrefixLength returns the length of the prefix that is added to every
  // file and used for storing encryption options. For optimal performance, the
  // prefix length should be a multiple of the page size.
  virtual size_t GetPrefixLength() override;

  // CreateNewPrefix initialized an allocated block of prefix memory
  // for a new file.
  virtual Status CreateNewPrefix(const std::string& fname, char* prefix,
                                 size_t prefixLength) override;

  // CreateCipherStream creates a block access cipher stream for a file given
  // given name and options.
  virtual Status CreateCipherStream(
      const std::string& fname, const EnvOptions& options, Slice& prefix,
      std::unique_ptr<BlockAccessCipherStream>* result) override;

 protected:
  // PopulateSecretPrefixPart initializes the data into a new prefix block
  // that will be encrypted. This function will store the data in plain text.
  // It will be encrypted later (before written to disk).
  // Returns the amount of space (starting from the start of the prefix)
  // that has been initialized.
  virtual size_t PopulateSecretPrefixPart(char* prefix, size_t prefixLength,
                                          size_t blockSize);

  // CreateCipherStreamFromPrefix creates a block access cipher stream for a
  // file given given name and options. The given prefix is already decrypted.
  virtual Status CreateCipherStreamFromPrefix(
      const std::string& fname, const EnvOptions& options,
      uint64_t initialCounter, const Slice& iv, const Slice& prefix,
      std::unique_ptr<BlockAccessCipherStream>* result);
};

}  // namespace rocksdb

#endif  // !defined(ROCKSDB_LITE)
