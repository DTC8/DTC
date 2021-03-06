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

#include <stdint.h>

#include <limits>
#include <string>
#include <vector>

#include "rocksdb/types.h"

namespace rocksdb {
struct ColumnFamilyMetaData;
struct LevelMetaData;
struct SstFileMetaData;

// The metadata that describes a column family.
struct ColumnFamilyMetaData {
  ColumnFamilyMetaData() : size(0), file_count(0), name("") {}
  ColumnFamilyMetaData(const std::string& _name, uint64_t size_,
                       const std::vector<LevelMetaData>&& _levels)
      : size(size_), name(_name), levels(_levels) {}

  // The size of this column family in bytes, which is equal to the sum of
  // the file size of its "levels".
  uint64_t size;
  // The number of files in this column family.
  size_t file_count;
  // The name of the column family.
  std::string name;
  // The metadata of all levels in this column family.
  std::vector<LevelMetaData> levels;
};

// The metadata that describes a level.
struct LevelMetaData {
  LevelMetaData(int _level, uint64_t size_,
                const std::vector<SstFileMetaData>&& _files)
      : level(_level), size(size_), files(_files) {}

  // The level which this meta data describes.
  const int level;
  // The size of this level in bytes, which is equal to the sum of
  // the file size of its "files".
  const uint64_t size;
  // The metadata of all sst files in this level.
  const std::vector<SstFileMetaData> files;
};

// The metadata that describes a SST file.
struct SstFileMetaData {
  SstFileMetaData()
      : size(0),
        file_number(0),
        smallest_seqno(0),
        largest_seqno(0),
        num_reads_sampled(0),
        being_compacted(false),
        num_entries(0),
        num_deletions(0),
        oldest_blob_file_number(0) {}

  SstFileMetaData(const std::string& _file_name, uint64_t _file_number,
                  const std::string& _path, size_t size_,
                  SequenceNumber _smallest_seqno, SequenceNumber _largest_seqno,
                  const std::string& _smallestkey,
                  const std::string& _largestkey, uint64_t _num_reads_sampled,
                  bool _being_compacted, uint64_t _oldest_blob_file_number,
                  uint64_t _oldest_ancester_time, uint64_t _file_creation_time)
      : size(size_),
        name(_file_name),
        file_number(_file_number),
        db_path(_path),
        smallest_seqno(_smallest_seqno),
        largest_seqno(_largest_seqno),
        smallestkey(_smallestkey),
        largestkey(_largestkey),
        num_reads_sampled(_num_reads_sampled),
        being_compacted(_being_compacted),
        num_entries(0),
        num_deletions(0),
        oldest_blob_file_number(_oldest_blob_file_number),
        oldest_ancester_time(_oldest_ancester_time),
        file_creation_time(_file_creation_time) {}

  // File size in bytes.
  size_t size;
  // The name of the file.
  std::string name;
  // The id of the file.
  uint64_t file_number;
  // The full path where the file locates.
  std::string db_path;

  SequenceNumber smallest_seqno;  // Smallest sequence number in file.
  SequenceNumber largest_seqno;   // Largest sequence number in file.
  std::string smallestkey;        // Smallest user defined key in the file.
  std::string largestkey;         // Largest user defined key in the file.
  uint64_t num_reads_sampled;     // How many times the file is read.
  bool being_compacted;  // true if the file is currently being compacted.

  uint64_t num_entries;
  uint64_t num_deletions;

  uint64_t oldest_blob_file_number;  // The id of the oldest blob file
                                     // referenced by the file.
  // An SST file may be generated by compactions whose input files may
  // in turn be generated by earlier compactions. The creation time of the
  // oldest SST file that is the compaction ancester of this file.
  // The timestamp is provided Env::GetCurrentTime().
  // 0 if the information is not available.
  uint64_t oldest_ancester_time;
  // Timestamp when the SST file is created, provided by Env::GetCurrentTime().
  // 0 if the information is not available.
  uint64_t file_creation_time;
};

// The full set of metadata associated with each SST file.
struct LiveFileMetaData : SstFileMetaData {
  std::string column_family_name;  // Name of the column family
  int level;                       // Level at which this file resides.
  LiveFileMetaData() : column_family_name(), level(0) {}
};

// Metadata returned as output from ExportColumnFamily() and used as input to
// CreateColumnFamiliesWithImport().
struct ExportImportFilesMetaData {
  std::string db_comparator_name;       // Used to safety check at import.
  std::vector<LiveFileMetaData> files;  // Vector of file metadata.
};
}  // namespace rocksdb
