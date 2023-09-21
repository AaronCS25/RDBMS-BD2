#include "HeapFile.hpp"
#include "Constants.hpp"
#include "Utils/File.hpp"

#include <numeric>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <utility>

constexpr int DEFAULT_DELETED = -1;

using DB_ENGINE::HeapFile;

HeapFile::HeapFile(std::string table_name, std::vector<Type> types,
                   std::vector<std::string> attribute_names,
                   std::string primary_key)
    : C_RECORD_SIZE(std::accumulate(
          types.begin(), types.end(), static_cast<uint8_t>(0),
          [](uint8_t sum, const Type &type) { return sum + type.size; })),
      m_table_name(std::move(table_name)),
      m_table_path(TABLES_PATH + m_table_name + "/"),
      m_metadata(std::move(attribute_names), std::move(types),
                 std::move(primary_key)) {

  open_or_create(m_table_path + DATA_FILE);

  if (!read_metadata()) {
    update_first_deleted(DEFAULT_DELETED);
  }
}

HeapFile::TableMetadata::TableMetadata(
    std::vector<std::string> _attribute_names,
    std::vector<Type> _attribute_types, std::string _primary_key)
    : attribute_names(std::move(_attribute_names)),
      attribute_types(std::move(_attribute_types)),
      primary_key(std::move(_primary_key)) {}

auto HeapFile::load() -> std::vector<Record> {

  m_file_stream.open(m_table_path + DATA_FILE, std::ios::binary | std::ios::in);
  std::vector<Record> records(m_metadata.record_count());

  Record::size_type curr_record = 0;
  while (
      records.at(curr_record).read(m_file_stream, m_metadata.attribute_types)) {
  }
  return records;
}

auto HeapFile::add(const Record & /*record*/) -> pos_type { return {}; }

auto HeapFile::next_pos() const -> pos_type { return {}; }

auto HeapFile::read(const pos_type &pos) -> Record {
  m_file_stream.open(m_table_path + DATA_FILE, std::ios::binary | std::ios::in);

  m_file_stream.seekg(pos, std::ios::beg);

  Record record;

  record.read(m_file_stream, m_metadata.attribute_types);

  return record;
}

auto HeapFile::remove(const pos_type & /*pos*/) -> bool { return {}; }

void HeapFile::update_first_deleted(pos_type pos) {
  m_metadata.first_deleted = pos;

  write_metadata();
}

auto HeapFile::filter(const Record & /*record*/,
                      const std::vector<std::string> & /*selected_attributes*/)
    const -> std::string {

  return {};
}

auto HeapFile::filter(const std::vector<Record> & /*record*/,
                      const std::vector<std::string> & /*selected_attributes*/)
    const -> std::vector<std::string> {
  return {};
}

void HeapFile::write_metadata() {
  std::ofstream metadata(m_table_path + METADATA_FILE, std::ios::binary);
  if (!metadata.write(reinterpret_cast<const char *>(&m_metadata),
                      sizeof(m_metadata))) {
    spdlog::error("Could not write metadata of table {}", m_table_name);
  }
  spdlog::info("Metadata of table {} written\n", m_table_name);
  spdlog::info(to_string());
  metadata.close();
}

auto HeapFile::TableMetadata::get_attribute_idx(
    const std::string &attribute_name) const -> uint8_t {

  auto counter = 0;
  for (const auto &attribute : attribute_names) {
    if (attribute == attribute_name) {
      return static_cast<uint8_t>(counter);
    }
    counter++;
  }
  throw std::runtime_error("Attribute not found");
}

auto HeapFile::string_cast(const Type &type, const char *data) -> std::string {

  switch (type.type) {
  case Type::BOOL: {
    bool value = *reinterpret_cast<const bool *>(data);
    return value ? "true" : "false";
  }

  case Type::INT: {
    int int_value = *reinterpret_cast<const int *>(data);
    return std::to_string(int_value);
  }
  case Type::FLOAT: {
    float float_value = *reinterpret_cast<const float *>(data);
    return std::to_string(float_value);
  }
  case Type::VARCHAR: {
    std::string value(data, type.size);
    return value;
  }
  }
  throw std::runtime_error("Invalid type");
}

auto HeapFile::get_record_size() const -> uint8_t { return C_RECORD_SIZE; }

auto HeapFile::get_type(const std::string &attribute_name) const -> Type {
  auto attribute_idx = m_metadata.get_attribute_idx(attribute_name);
  return m_metadata.attribute_types.at(attribute_idx);
}

auto HeapFile::get_type(const Attribute &attribute) const -> Type {
  return get_type(attribute.name);
}
auto HeapFile::get_types() const -> std::vector<Type> {
  return m_metadata.attribute_types;
}

auto HeapFile::get_key(const Record &record) const
    -> std::pair<Type, Attribute> {

  auto key_idx = m_metadata.get_attribute_idx(m_metadata.primary_key);

  auto key_type = m_metadata.attribute_types.at(key_idx);

  std::string key_value =
      string_cast(key_type, record.m_fields.at(key_idx).data());

  Attribute key_attribute = {m_metadata.attribute_names.at(key_idx), key_value};

  return {key_type, key_attribute};
}

auto HeapFile::get_key_name() const -> std::pair<Type, std::string> {

  auto key_idx = m_metadata.get_attribute_idx(m_metadata.primary_key);
  auto key_type = m_metadata.attribute_types.at(key_idx);
  return {key_type, m_metadata.attribute_names.at(key_idx)};
}
auto HeapFile::get_key_idx() const -> uint8_t {
  return m_metadata.get_attribute_idx(m_metadata.primary_key);
}

auto HeapFile::get_attribute_names() const -> std::vector<std::string> {
  return m_metadata.attribute_names;
}
auto HeapFile::read_metadata() -> bool {

  spdlog::info("Reading metadata from table {}", m_table_name);

  open_or_create(m_table_path + METADATA_FILE);

  std::ifstream file(
      m_table_path + METADATA_FILE,
      std::ios::binary |
          std::ios::ate); // Already has been checked that it exists

  // File must contain a single integer
  std::streamsize size = file.tellg();

  if (size != sizeof(TableMetadata)) {
    spdlog::error("Metadata file {} is corrupted or empty",
                  m_table_path + METADATA_FILE);
    file.close();
    return false;
  }

  file.seekg(0, std::ios::beg);

  // The file must be readable
  if (!file.read(reinterpret_cast<char *>(&m_metadata), sizeof(int))) {
    spdlog::error("Error reading metadata from file {}",
                  m_table_path + METADATA_FILE);
    return false;
  }

  return true;
}
