#include "ordered_map_file.hpp"
#include "crc32.hpp"

static const int UUID_SIZE = 16;
static const char *UUID = "\xca\x2f\x5e\xf5\x00\xd8\xef\x0b\x80\x74\x18\xd0\xe4\x0b\x7a\x4f";

static const int TRANSACTION_METADATA_SIZE = 16;

static void write_uint32be(uint8_t *buf, uint32_t x) {
    buf[3] = x & 0xff;

    x >>= 8;
    buf[2] = x & 0xff;

    x >>= 8;
    buf[1] = x & 0xff;

    x >>= 8;
    buf[0] = x & 0xff;
}

static uint32_t read_uint32be(const uint8_t *buf) {
    uint32_t result = buf[0];

    result <<= 8;
    result |= buf[1];

    result <<= 8;
    result |= buf[2];

    result <<= 8;
    result |= buf[3];

    return result;
}

static int get_transaction_size(OrderedMapFileBatch *batch) {
    int total = TRANSACTION_METADATA_SIZE;
    for (int i = 0; i < batch->puts.length(); i += 1) {
        OrderedMapFilePut *put = &batch->puts.at(i);
        total += 8 + put->key->size + put->value->size;
    }
    for (int i = 0; i < batch->dels.length(); i += 1) {
        OrderedMapFileDel *del = &batch->dels.at(i);
        total += 4 + del->key->size;

    }
    return total;
}

static void run_write(void *userdata) {
    OrderedMapFile *omf = (OrderedMapFile *)userdata;

    for (;;) {
        OrderedMapFileBatch *batch = nullptr;
        omf->queue.shift(&batch);
        if (!batch || !omf->running)
            break;

        // compute transaction size
        int transaction_size = get_transaction_size(batch);
        omf->write_buffer.resize(transaction_size);

        uint8_t *transaction_ptr = (uint8_t*)omf->write_buffer.raw();
        write_uint32be(&transaction_ptr[4], transaction_size);
        write_uint32be(&transaction_ptr[8], batch->puts.length());
        write_uint32be(&transaction_ptr[12], batch->dels.length());

        int offset = TRANSACTION_METADATA_SIZE;
        for (int i = 0; i < batch->puts.length(); i += 1) {
            OrderedMapFilePut *put = &batch->puts.at(i);
            write_uint32be(&transaction_ptr[offset], put->key->size); offset += 4;
            write_uint32be(&transaction_ptr[offset], put->value->size); offset += 4;
            memcpy(&transaction_ptr[offset], put->key->data, put->key->size); offset += put->key->size;
            memcpy(&transaction_ptr[offset], put->value->data, put->value->size); offset += put->value->size;
        }
        for (int i = 0; i < batch->dels.length(); i += 1) {
            OrderedMapFileDel *del = &batch->dels.at(i);
            write_uint32be(&transaction_ptr[offset], del->key->size); offset += 4;
            memcpy(&transaction_ptr[offset], del->key->data, del->key->size); offset += del->key->size;
        }
        assert(offset == transaction_size);

        ordered_map_file_batch_destroy(batch);

        // compute crc32
        write_uint32be(&transaction_ptr[0], crc32(0, &transaction_ptr[4], transaction_size - 4));

        // append to file
        size_t amt_written = fwrite(transaction_ptr, 1, transaction_size, omf->file);
        if (amt_written != (size_t)transaction_size)
            panic("write to disk failed");
    }
}

static int read_header(OrderedMapFile *omf) {
    char uuid_buf[UUID_SIZE];
    int amt_read = fread(uuid_buf, 1, UUID_SIZE, omf->file);

    if (amt_read == 0)
        return GenesisErrorEmptyFile;

    if (amt_read != UUID_SIZE)
        return GenesisErrorInvalidFormat;

    if (memcmp(UUID, uuid_buf, UUID_SIZE) != 0)
        return GenesisErrorInvalidFormat;

    return 0;
}

static int compare_entries(OrderedMapFileEntry * a, OrderedMapFileEntry * b) {
    return ByteBuffer::compare(a->key, b->key);
}

static void destroy_map(OrderedMapFile *omf) {
    if (omf->map) {
        auto it = omf->map->entry_iterator();
        for (;;) {
            auto *map_entry = it.next();
            if (!map_entry)
                break;

            OrderedMapFileEntry *omf_entry = map_entry->value;
            destroy(omf_entry, 1);
        }
        destroy(omf->map, 1);
        omf->map = nullptr;
    }
}

int ordered_map_file_open(const char *path, OrderedMapFile **out_omf) {
    *out_omf = nullptr;
    OrderedMapFile *omf = create_zero<OrderedMapFile>();
    if (!omf) {
        ordered_map_file_close(omf);
        return GenesisErrorNoMem;
    }
    if (omf->queue.error()) {
        ordered_map_file_close(omf);
        return omf->queue.error();
    }
    omf->list = create_zero<List<OrderedMapFileEntry *>>();
    if (!omf->list) {
        ordered_map_file_close(omf);
        return GenesisErrorNoMem;
    }

    omf->running = true;
    int err = omf->write_thread.start(run_write, omf);
    if (err) {
        ordered_map_file_close(omf);
        return err;
    }

    bool open_for_writing = false;
    omf->file = fopen(path, "rb+");
    if (omf->file) {
        int err = read_header(omf);
        if (err == GenesisErrorEmptyFile) {
            open_for_writing = true;
        } else if (err) {
            ordered_map_file_close(omf);
            return err;
        }
    } else {
        open_for_writing = true;
    }
    if (open_for_writing) {
        omf->file = fopen(path, "wb+");
        if (!omf->file) {
            ordered_map_file_close(omf);
            return GenesisErrorFileAccess;
        }
    }

    omf->map = create_zero<HashMap<ByteBuffer, OrderedMapFileEntry *, ByteBuffer::hash>>();
    if (!omf->map) {
        ordered_map_file_close(omf);
        return GenesisErrorNoMem;
    }

    // read everything into list
    omf->write_buffer.resize(TRANSACTION_METADATA_SIZE);
    omf->transaction_offset = UUID_SIZE;
    for (;;) {
        size_t amt_read = fread(omf->write_buffer.raw(), 1, TRANSACTION_METADATA_SIZE, omf->file);
        if (amt_read != TRANSACTION_METADATA_SIZE) {
            // partial transaction. ignore it and we're done.
            break;
        }
        uint8_t *transaction_ptr = (uint8_t*)omf->write_buffer.raw();
        int transaction_size = read_uint32be(&transaction_ptr[4]);

        omf->write_buffer.resize(transaction_size);
        transaction_ptr = (uint8_t*)omf->write_buffer.raw();

        size_t amt_to_read = transaction_size - TRANSACTION_METADATA_SIZE;
        amt_read = fread(&transaction_ptr[TRANSACTION_METADATA_SIZE], 1, amt_to_read, omf->file);
        if (amt_read != amt_to_read) {
            // partial transaction. ignore it and we're done.
            break;
        }
        uint32_t computed_crc = crc32(0, &transaction_ptr[4], transaction_size - 4);
        uint32_t crc_from_file = read_uint32be(&transaction_ptr[0]);
        if (computed_crc != crc_from_file) {
            // crc check failed. ignore this transaction and we're done.
            break;
        }

        omf->transaction_offset += transaction_size;

        int put_count = read_uint32be(&transaction_ptr[8]);
        int del_count = read_uint32be(&transaction_ptr[12]);

        int offset = TRANSACTION_METADATA_SIZE;
        for (int i = 0; i < put_count; i += 1) {
            int key_size = read_uint32be(&transaction_ptr[offset]); offset += 4;
            int val_size = read_uint32be(&transaction_ptr[offset]); offset += 4;

            OrderedMapFileEntry *entry = create_zero<OrderedMapFileEntry>();
            if (!entry) {
                ordered_map_file_close(omf);
                return GenesisErrorNoMem;
            }

            entry->key = ByteBuffer((char*)&transaction_ptr[offset], key_size); offset += key_size;
            entry->offset = offset;
            entry->size = val_size;
            offset += val_size;

            omf->map->put(entry->key, entry);
        }
        for (int i = 0; i < del_count; i += 1) {
            int key_size = read_uint32be(&transaction_ptr[offset]); offset += 4;
            ByteBuffer key((char*)&transaction_ptr[offset], key_size); offset += key_size;

            auto hash_entry = omf->map->maybe_get(key);
            if (hash_entry) {
                OrderedMapFileEntry *entry = hash_entry->value;
                omf->map->remove(key);
                destroy(entry, 1);
            }
        }
    }

    // transfer map to list and sort
    auto it = omf->map->entry_iterator();
    for (;;) {
        auto *map_entry = it.next();
        if (!map_entry)
            break;

        if (omf->list->append(map_entry->value)) {
            ordered_map_file_close(omf);
            return GenesisErrorNoMem;
        }
    }
    destroy_map(omf);

    insertion_sort<OrderedMapFileEntry *, compare_entries>(omf->list->raw(), omf->list->length());

    *out_omf = omf;
    return 0;
}

static void destroy_list(OrderedMapFile *omf) {
    destroy(omf->list, 1);
    omf->list = nullptr;
}

void ordered_map_file_close(OrderedMapFile *omf) {
    if (omf) {
        omf->running = false;
        omf->queue.wakeup_all();
        omf->write_thread.join();
        destroy_list(omf);
        destroy_map(omf);
        fclose(omf->file);
        destroy(omf, 1);
    }
}

OrderedMapFileBatch *ordered_map_file_batch_create(OrderedMapFile *omf) {
    OrderedMapFileBatch *batch = create_zero<OrderedMapFileBatch>();
    if (!batch) {
        ordered_map_file_batch_destroy(batch);
        return nullptr;
    }
    batch->omf = omf;
    return batch;
}

void ordered_map_file_batch_destroy(OrderedMapFileBatch *batch) {
    if (batch) {
        for (int i = 0; i < batch->puts.length(); i += 1) {
            OrderedMapFilePut *put = &batch->puts.at(i);
            ordered_map_file_buffer_destroy(put->key);
            ordered_map_file_buffer_destroy(put->value);
        }
        for (int i = 0; i < batch->dels.length(); i += 1) {
            OrderedMapFileDel *del = &batch->dels.at(i);
            ordered_map_file_buffer_destroy(del->key);
        }
        destroy(batch, 1);
    }
}

int ordered_map_file_batch_exec(OrderedMapFileBatch *batch) {
    return batch->omf->queue.push(batch);
}

OrderedMapFileBuffer *ordered_map_file_buffer_create(int size) {
    OrderedMapFileBuffer *buffer = create_zero<OrderedMapFileBuffer>();
    if (!buffer) {
        ordered_map_file_buffer_destroy(buffer);
        return nullptr;
    }

    buffer->size = size;
    buffer->data = allocate_safe<char>(size);
    if (!buffer->data) {
        ordered_map_file_buffer_destroy(buffer);
        return nullptr;
    }

    return buffer;
}

void ordered_map_file_buffer_destroy(OrderedMapFileBuffer *buffer) {
    if (buffer) {
        destroy(buffer->data, buffer->size);
        destroy(buffer, 1);
    }
}

int ordered_map_file_batch_put(OrderedMapFileBatch *batch,
        OrderedMapFileBuffer *key, OrderedMapFileBuffer *value)
{
    if (batch->puts.resize(batch->puts.length() + 1))
        return GenesisErrorNoMem;

    OrderedMapFilePut *put = &batch->puts.at(batch->puts.length() - 1);
    put->key = key;
    put->value = value;
    return 0;
}

int ordered_map_file_batch_del(OrderedMapFileBatch *batch,
        OrderedMapFileBuffer *key)
{
    if (batch->dels.resize(batch->dels.length() + 1))
        return GenesisErrorNoMem;

    OrderedMapFileDel *del = &batch->dels.at(batch->dels.length() - 1);
    del->key = key;
    return 0;
}

void ordered_map_file_done_reading(OrderedMapFile *omf) {
    destroy_list(omf);
    if (fseek(omf->file, omf->transaction_offset, SEEK_SET))
        panic("unable to seek in file");
}