#include <file.h>
#include <cpp/Exception.h>

#ifndef FILESYSTEM_DISABLE_ZIP
#include <zip.h>
#endif

#include <vector>
#include <string>
#include <algorithm>

#include <cstdio>
#include <cstring>
#include <cstdint>

using namespace kx;

// FileSystem

struct FileSystem::impl
{
    std::vector<std::unique_ptr<FileSystemHandler>> handlers;
};

FileSystem::FileSystem ()
    : my(new impl) {}

FileSystem::FileSystem (const char* path)
    : my(new impl)
{
    addHandler(std::move(std::unique_ptr<FileSystemHandler>(new RegularFileSystem(path))));
}

FileSystem::FileSystem (const std::vector<FilePath>& paths)
    : my(new impl)
{
    for (const char* path : paths)
        addHandler(std::move(std::unique_ptr<FileSystemHandler>(new RegularFileSystem(path))));
}

FileSystem::FileSystem (FileSystem&& filesystem)
    : my(std::move(filesystem.my)) {}

FileSystem& FileSystem::operator= (FileSystem&& filesystem)
{
    my = std::move(filesystem.my);
    return *this;
}

FileSystem::~FileSystem () {}

File FileSystem::open(const FilePath& filepath) const
{
    for (auto& handler : my->handlers)
    {
        FileHandler* file = handler->open(filepath);
        if (file != nullptr)
            return File(std::move(std::unique_ptr<FileHandler>(file)));
    }
    // Throw exception only after trying all handlers
    std::ostringstream os;
    os << "Failed opening file " << filepath;
    throw EXCEPTION(os);
}

void FileSystem::addHandler (std::unique_ptr<FileSystemHandler> handler)
{
    my->handlers.push_back(std::move(handler));
}

// File

File::File (std::unique_ptr<FileHandler> handler)
    : handler(std::move(handler)) {}

File::File (File&& file)
{
    handler = std::move(file.handler);
}

File& File::operator=(File&& file)
{
    handler = std::move(file.handler);
    return *this;
}

File::~File () {}

std::string File::read_all ()
{
    std::string contents(this->size(), 0);
    read(&contents[0], contents.size());
    return contents;
}

std::size_t File::read (void* buffer, std::size_t size)
{
    return handler->read(buffer, size);
}

std::size_t File::read_line (char *buffer, std::size_t count)
{
    char c = 0;
    int chars_read = 0;
    for (; count > 0 && c != '\n'; --count)
    {
        handler->read(&c, 1);
        if (c != '\r' && c != '\n')
        {
            *buffer++ = c;
            chars_read++;
        }
    }
    if (c != '\n') ++buffer;
    *buffer = 0;
    return chars_read;
}

std::uint8_t File::get ()
{
    std::uint8_t c;
    handler->read(&c, 1);
    return c;
}

std::uint8_t File::peek ()
{
    std::uint8_t c = get();
    handler->seek(-1, std::ios::cur);
    return c;
}

void File::seek (std::ios::off_type offset, std::ios::seekdir origin)
{
    handler->seek(offset, origin);
}

std::ios::pos_type File::tell () const
{
    return handler->tell();
}

std::size_t File::size () const
{
    return handler->size();
}

bool File::eof() const
{
    return (std::size_t) handler->tell() == handler->size();
}

//
// File system implementations
//

// RegularFileSystem

RegularFileSystem::RegularFileSystem (const Path& root)
    : root(root) {}

FileHandler* RegularFileSystem::open (const FilePath& filepath)
{
    std::string filepath_ = std::string(root) + "/" + filepath;
    std::unique_ptr<std::ifstream> f(new std::ifstream(filepath_, std::ios::binary));
    if (f->is_open())
        return new RegularFile(std::move(f));
    else
        return nullptr;
}

// ZipFileSystem

ZipFileSystem::ZipFileSystem (const Path& zip_file)
    : zip_file(zip_file) {}

FileHandler* ZipFileSystem::open (const FilePath& filepath)
{
#ifndef FILESYSTEM_DISABLE_ZIP
    zip* z = zip_open(filepath, 0, NULL);
    if (z != NULL)
    {
        struct zip_stat stat;
        zip_stat(z, filepath, 0, &stat);
        struct zip_file* file = zip_fopen(z, filepath, 0);
        if (file != NULL)
        {
            std::size_t size = stat.size;
            std::unique_ptr<std::uint8_t> data(new std::uint8_t[size]);
            zip_fread(file, data.get(), stat.size);
            zip_fclose(file);
            zip_close(z);
            return new MemFile(std::move(data), size);
        }
        zip_close(z);
    }
    return nullptr;
#else
    throw EXCEPTION("zip files not supported in this FileSystem build");
#endif
}

//
// File implementations
//

// MemFile

struct MemFile::impl
{
    // This is used when the MemFile takes ownership of the file data.
    // Otherwise remains null.
    std::unique_ptr<std::uint8_t> data;

    // We use std::uint8_t* so that we can compute byte offsets
    // by subtracting pointers
    const std::uint8_t* beg; // points to the beginning of the file
    const std::uint8_t* pointer; // points to the current position

    std::size_t size; // file size

    impl (std::unique_ptr<std::uint8_t> data, std::size_t size)
        : data(std::move(data)), beg(data.get()), pointer(beg), size(size) {}

    impl (void* data, std::size_t size)
        : beg((std::uint8_t*)data), pointer(beg), size(size) {}
};

MemFile::MemFile (std::unique_ptr<std::uint8_t> data, std::size_t size)
    : my(new impl(std::move(data), size)) {}

MemFile::MemFile (void* data, std::size_t size)
    : my(new impl(data, size)) {}

MemFile::~MemFile () {}

std::size_t MemFile::read (void* buffer, std::size_t size)
{
    std::size_t remaining = my->beg + my->size - my->pointer;
    std::size_t read = std::min(remaining, size);
    memcpy(buffer, my->pointer, read);
    my->pointer += read;
    return read/my->size;
}

void MemFile::seek (std::ios::off_type offset, std::ios::seekdir origin)
{
    if (origin == std::ios::beg) my->pointer = my->beg + offset;
    else if (origin == std::ios::cur) my->pointer += offset;
    else my->pointer = std::min(my->beg + my->size + offset, my->beg + my->size);
}

std::ios::pos_type MemFile::tell () const
{
    return (std::ios::pos_type) (my->pointer - my->beg);
}

std::size_t MemFile::size () const
{
    return my->size;
}

// RegularFile

RegularFile::RegularFile (std::unique_ptr<std::ifstream> file_)
    : file(std::move(file_))
{
    // Compute file size.
    file->seekg(0, std::ios::end);
    size_ = file->tellg();
    file->seekg(0, std::ios::beg);
}

std::size_t RegularFile::read (void* buffer, std::size_t size)
{
    file->read((char*)buffer, (std::streamsize) size);
    return file->gcount();
}

void RegularFile::seek (std::ios::off_type offset, std::ios::seekdir origin)
{
    file->seekg(offset, origin);
}

std::ios::pos_type RegularFile::tell () const
{
    return file->tellg();
}

std::size_t RegularFile::size () const
{
    return size_;
}
