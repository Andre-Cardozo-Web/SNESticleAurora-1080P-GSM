#include "miniz_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEWLIB_PORT_AWARE
#include <io_common.h>
#include <fileXio.h>
#include <fileXio_rpc.h>
#undef NEWLIB_PORT_AWARE

#include "miniz.h"

/* All ROM-source paths reachable by the project are exposed by iomanX.
   Slurp the compressed file once and hand it to miniz's memory reader, but
   use direct fileXio calls: one large EE->IOP read avoids the repeated small
   stdio RPCs that made ZIP/GZ launch noticeably slower on cdfs/mass/SMB. */

/* AURORA_ZIP_FILEXIO_STREAM_V1_20260822
 *
 * fileXioRead(), like read(), is allowed to complete a request partially.
 * Keep the existing fast direct-IOP path, but finish the request instead of
 * treating the first short transfer as a corrupt archive.
 */
static int filexio_read_fully(int fd, void *buf, int size)
{
        unsigned char *dst = (unsigned char *)buf;
        int total = 0;

        if (fd < 0 || !buf || size < 0)
                return -1;

        while (total < size)
        {
                int n = fileXioRead(fd, dst + total, size - total);
                if (n <= 0)
                        return (total > 0) ? total : n;
                total += n;
        }

        return total;
}

/* Use miniz's user-supplied random-access reader for ZIP archives.
 *
 * The previous implementation malloc()'d a second buffer as large as the
 * complete .zip before decompression. Aurora now keeps the SNES, QuickNES
 * and PicoDrive frontends in the same 32 MiB PS2 process, so that temporary
 * whole-archive copy can fail even when the extracted ROM itself fits in
 * the permanent _RomData buffer.
 */
typedef struct MinizFileXioReader
{
        int fd;
        int size;
        int pos;
} MinizFileXioReader;

static int miniz_filexio_reader_open(
        const char *path, MinizFileXioReader *reader)
{
        int size;

        if (!reader)
                return -1;

        reader->fd = -1;
        reader->size = 0;
        reader->pos = 0;

        if (!path || !path[0])
                return -1;

        reader->fd = fileXioOpen(path, FIO_O_RDONLY, 0);
        if (reader->fd < 0)
                return -1;

        size = fileXioLseek(reader->fd, 0, FIO_SEEK_END);
        if (size <= 0)
        {
                fileXioClose(reader->fd);
                reader->fd = -1;
                return -1;
        }

        if (fileXioLseek(reader->fd, 0, FIO_SEEK_SET) < 0)
        {
                fileXioClose(reader->fd);
                reader->fd = -1;
                return -1;
        }

        reader->size = size;
        reader->pos = 0;
        return size;
}

static void miniz_filexio_reader_close(MinizFileXioReader *reader)
{
        if (!reader)
                return;

        if (reader->fd >= 0)
                fileXioClose(reader->fd);

        reader->fd = -1;
        reader->size = 0;
        reader->pos = 0;
}

static size_t miniz_filexio_read_func(
        void *opaque, mz_uint64 file_ofs, void *buf, size_t n)
{
        MinizFileXioReader *reader = (MinizFileXioReader *)opaque;
        unsigned char *dst = (unsigned char *)buf;
        size_t total = 0;
        size_t avail;

        if (!reader || reader->fd < 0 || !buf || n == 0)
                return 0;

        if (file_ofs > 0x7FFFFFFFULL ||
            file_ofs >= (mz_uint64)reader->size)
                return 0;

        avail = (size_t)((mz_uint64)reader->size - file_ofs);
        if (n > avail)
                n = avail;

        if (reader->pos != (int)file_ofs)
        {
                int pos = fileXioLseek(
                        reader->fd, (int)file_ofs, FIO_SEEK_SET);
                if (pos < 0)
                        return 0;
                reader->pos = pos;
        }

        while (total < n)
        {
                size_t remain = n - total;
                int want = (remain > 0x7FFFFFFFUL)
                        ? 0x7FFFFFFF : (int)remain;
                int got = fileXioRead(reader->fd, dst + total, want);

                if (got <= 0)
                        break;

                total += (size_t)got;
                reader->pos += got;
        }

        return total;
}

static int read_file_to_alloc(const char *path, void **out_buf, int *out_size)
{
        int fd;
        int size;
        int n;
        void *buf;

        if (!path || !path[0])
                return -1;

        /* Direct fileXio calls use IOP flags, not newlib/POSIX flags.
           FIO_O_RDONLY is 1 while newlib O_RDONLY is 0. */
        fd = fileXioOpen(path, FIO_O_RDONLY, 0);
        if (fd < 0)
                return -1;

        size = fileXioLseek(fd, 0, FIO_SEEK_END);
        if (size <= 0)
        {
                fileXioClose(fd);
                return -1;
        }
        if (fileXioLseek(fd, 0, FIO_SEEK_SET) < 0)
        {
                fileXioClose(fd);
                return -1;
        }

        buf = malloc((size_t)size);
        if (!buf)
        {
                fileXioClose(fd);
                return -1;
        }

        n = filexio_read_fully(fd, buf, size);
        fileXioClose(fd);

        if (n != size)
        {
                free(buf);
                return -1;
        }

        *out_buf = buf;
        *out_size = (int)size;
        return (int)size;
}

/* Walks the variable-length gzip header (RFC 1952 section 2.3) and
   returns the byte offset where the raw DEFLATE stream starts, or -1
   if the buffer doesn't look like a valid gzip stream. */
static int parse_gzip_header(const unsigned char *data, int len)
{
        unsigned char flags;
        int off;

        if (len < 10)
                return -1;
        if (data[0] != 0x1F || data[1] != 0x8B || data[2] != 8)
                return -1;

        flags = data[3];
        off = 10;

        if (flags & 0x04) /* FEXTRA */
        {
                int xlen;
                if (off + 2 > len) return -1;
                xlen = data[off] | (data[off + 1] << 8);
                off += 2 + xlen;
                if (off > len) return -1;
        }
        if (flags & 0x08) /* FNAME */
        {
                while (off < len && data[off] != 0) off++;
                if (off >= len) return -1;
                off++;
        }
        if (flags & 0x10) /* FCOMMENT */
        {
                while (off < len && data[off] != 0) off++;
                if (off >= len) return -1;
                off++;
        }
        if (flags & 0x02) /* FHCRC */
        {
                off += 2;
                if (off > len) return -1;
        }
        return off;
}

int MinizReadGZToBuffer(const char *path, void *out_buf, int out_max)
{
        void *gz_data = NULL;
        int gz_size = 0;
        int hdr;
        int deflate_len;
        size_t produced;

        if (!out_buf || out_max <= 0)
                return -1;

        if (read_file_to_alloc(path, &gz_data, &gz_size) <= 0)
                return -1;

        hdr = parse_gzip_header((const unsigned char *)gz_data, gz_size);
        if (hdr < 0 || gz_size <= hdr + 8)
        {
                free(gz_data);
                return -1;
        }

        /* The DEFLATE payload lives between [hdr .. gz_size-8); the
           trailing 8 bytes are the gzip CRC32 + ISIZE which miniz
           does not need. */
        deflate_len = gz_size - hdr - 8;

        produced = tinfl_decompress_mem_to_mem(
                out_buf, (size_t)out_max,
                (const unsigned char *)gz_data + hdr, (size_t)deflate_len,
                0 /* raw deflate, not zlib-wrapped */);

        free(gz_data);

        if (produced == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED)
                return -1;

        return (int)produced;
}

int MinizReadZipFirstMatch(const char *path,
                           void *out_buf,
                           int out_max,
                           char *out_filename,
                           int filename_max,
                           int (*name_filter)(const char *name))
{
        MinizFileXioReader reader;
        mz_zip_archive zip;
        mz_uint num_files;
        mz_uint i;
        int result = -1;

        if (!out_buf || out_max <= 0)
                return -1;

        if (miniz_filexio_reader_open(path, &reader) <= 0)
                return -1;

        memset(&zip, 0, sizeof(zip));
        zip.m_pRead = miniz_filexio_read_func;
        zip.m_pIO_opaque = &reader;

        if (!mz_zip_reader_init(&zip, (mz_uint64)reader.size, 0))
        {
                miniz_filexio_reader_close(&reader);
                return -1;
        }

        num_files = mz_zip_reader_get_num_files(&zip);
        for (i = 0; i < num_files; i++)
        {
                mz_zip_archive_file_stat st;
                if (!mz_zip_reader_file_stat(&zip, i, &st))
                        continue;
                if (st.m_is_directory)
                        continue;
                if (st.m_uncomp_size == 0)
                        continue;
                if (st.m_uncomp_size > (mz_uint64)out_max)
                        continue;
                if (name_filter && !name_filter(st.m_filename))
                        continue;

                if (mz_zip_reader_extract_to_mem(
                                &zip, i, out_buf,
                                (size_t)st.m_uncomp_size, 0))
                {
                        result = (int)st.m_uncomp_size;
                        if (out_filename && filename_max > 0)
                        {
                                strncpy(out_filename, st.m_filename,
                                        (size_t)(filename_max - 1));
                                out_filename[filename_max - 1] = '\0';
                        }
                        break;
                }
        }

        mz_zip_reader_end(&zip);
        miniz_filexio_reader_close(&reader);
        return result;
}
