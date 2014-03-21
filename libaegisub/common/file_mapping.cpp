// Copyright (c) 2014, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

#include "../config.h"

#include "libaegisub/file_mapping.h"

#include "libaegisub/fs.h"
#include "libaegisub/util.h"

#include <boost/filesystem.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <limits>

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace boost::interprocess;

namespace agi {
file_mapping::file_mapping(agi::fs::path const& filename, boost::interprocess::mode_t mode)
#ifdef _WIN32
: handle(CreateFileW(filename.wstring().c_str(), (unsigned int)mode, 0, nullptr, OPEN_EXISTING, 0, 0))
{
	if (handle == ipcdetail::invalid_file()) {
		switch (GetLastError()) {
		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
			throw fs::FileNotFound(filename);
		case ERROR_ACCESS_DENIED:
			throw fs::ReadDenied(filename);
		default:
			throw fs::FileSystemUnknownError(util::ErrorString(GetLastError()));
		}
	}
#else
: handle(ipcdetail::open_existing_file(filename.string().c_str(), mode))
{
	if (handle == ipcdetail::invalid_file()) {
		switch (errno) {
		case ENOENT:
			throw fs::FileNotFound(filename);
		case EACCES:
			throw fs::ReadDenied(filename);
		case EIO:
			throw fs::FileSystemUnknownError("Fatal I/O opening path: " + filename.string());
		}
	}
#endif
}

file_mapping::~file_mapping() {
	if (handle != ipcdetail::invalid_file()) {
		ipcdetail::close_file(handle);
	}
}

read_file_mapping::read_file_mapping(fs::path const& filename)
: file(filename, read_only)
{
	offset_t size;
	ipcdetail::get_file_size(file.get_mapping_handle().handle, size);
	file_size = static_cast<uint64_t>(size);
}

read_file_mapping::~read_file_mapping() { }

char *read_file_mapping::read(int64_t s_offset, uint64_t length) {
	auto offset = static_cast<uint64_t>(s_offset);
	if (offset + length > file_size)
		throw InternalError("Attempted to map beyond end of file", nullptr);

	// Check if we can just use the current mapping
	if (region && offset >= mapping_start && offset + length <= mapping_start + region->get_size())
		return static_cast<char *>(region->get_address()) + offset - mapping_start;

	if (sizeof(size_t) == 4) {
		mapping_start = offset & ~0xFFFFFULL; // Align to 1 MB bondary
		length += static_cast<size_t>(offset - mapping_start);
		// Map 16 MB or length rounded up to the next MB
		length = std::min<uint64_t>(std::max<uint64_t>(0x1000000U, (length + 0xFFFFF) & ~0xFFFFF), file_size - mapping_start);
	}
	else {
		// Just map the whole file
		mapping_start = 0;
		length = file_size;
	}

	if (length > std::numeric_limits<size_t>::max())
		throw std::bad_alloc();

	try {
		region = agi::util::make_unique<mapped_region>(file, read_only, mapping_start, static_cast<size_t>(length));
	}
	catch (interprocess_exception const&) {
		throw fs::FileSystemUnknownError("Failed mapping a view of the file");
	}

	return static_cast<char *>(region->get_address()) + offset - mapping_start;
}
}