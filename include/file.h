#pragma once

#include <cpp/cpp.h>
#include <memory>
#include <vector>
#include <fstream>
#include <string>
#include <cstdint>

namespace kx
{

class File;
class FileSystemHandler;
class FileHandler;

using Path = const char*;
using FilePath = const char*;

class FileSystem : NonCopyable
{
public:

    /// Construct an empty file system.
    FileSystem ();

    /// Construct a file system to load files from the given path.
    explicit FileSystem (FilePath);

    /// Construct a file system to load files from the given paths.
    explicit FileSystem (const std::vector<FilePath>&);

    FileSystem (FileSystem&&) = default;

    FileSystem& operator= (FileSystem&&) = default;

    ~FileSystem ();

    /// Open a file in the file system.
    /// Throw an exception if the file cannot be found.
    File open (const FilePath&) const;

    void addHandler (std::unique_ptr<FileSystemHandler>);

private:

    struct impl;
    std::unique_ptr<impl> my;
};

class File : NonCopyable
{
public:

    File (File&&);

    File& operator= (File&&);

    ~File ();

    /// Read the entire file and return its contents as a string.
    std::string read_all ();

    /// Attempt to read 'size' bytes into the buffer.
    /// Return the number of bytes read.
    std::size_t read (void* buffer, std::size_t size);

    /// Attempt to read 'size' bytes into the buffer or until a newline is found.
    /// Return the number of bytes read.
    std::size_t read_line (char* buffer, std::size_t count);

    /// Read a byte from the file.
    std::uint8_t get ();

    /// Peek at the next byte in the file.
    /// Does not consume any bytes from the file stream.
    std::uint8_t peek ();

    /// Advance 'offset' bytes from the given position
    void seek (std::ios::off_type offset, std::ios_base::seekdir);

    /// Return the input position indicator.
    std::ios::pos_type tell () const;

    /// Return the size of the file.
    std::size_t size () const;

    /// Return true if the input position indicator has reached end-of-file,
    /// false otherwise.
    bool eof () const;

private:

    friend class FileSystem;
    File (std::unique_ptr<FileHandler>);

    std::unique_ptr<FileHandler> handler;
};

//
// Interfaces
//

/// A common interface for file system implementations.
class FileSystemHandler : unique
{
public:

    virtual ~FileSystemHandler() {}

    /// Open the given file.
    /// Return null on failure.
    virtual FileHandler* open (const FilePath&) = 0;
};

/// A common interface for file implementations.
class FileHandler : unique
{
public:

    virtual ~FileHandler() {}
    virtual std::size_t read (void* buffer, std::size_t size) = 0;
    virtual void seek (std::ios::off_type offset, std::ios_base::seekdir) = 0;
    virtual std::ios::pos_type tell () const = 0;
    virtual std::size_t size () const = 0;
};

//
// File system implementations
//

/// A file system that can load files from the hard drive.
class RegularFileSystem final : public FileSystemHandler
{
public:

    RegularFileSystem (const Path& root);

    FileHandler* open (const FilePath&);

private:

    const Path root;
};

/// A file system that can load files from zip files.
class ZipFileSystem final : public FileSystemHandler
{
public:

    ZipFileSystem (const Path& zip_file);

    FileHandler* open (const FilePath&);

private:

    const Path zip_file;
};

//
// File implementations
//

/// A file in memory.
class MemFile final : public FileHandler
{
public:

    /// Construct a MemFile.
    /// The MemFile takes ownership of the data.
    MemFile (std::unique_ptr<std::uint8_t> data, std::size_t size);

    /// Construct a MemFile.
    /// The MemFile does not take ownership of the data.
    MemFile (void* data, std::size_t size);

    ~MemFile ();

    std::size_t read (void* buffer, std::size_t size) override;
    void seek (std::ios::off_type offset, std::ios::seekdir origin) override;
    std::ios::pos_type tell () const override;
    std::size_t size () const override;

private:

    struct impl;
    std::unique_ptr<impl> my;
};

/// A FileHandler wrapper for an std::ifstream.
class RegularFile final : public FileHandler
{
public:

    RegularFile (std::unique_ptr<std::ifstream>);

    std::size_t read (void* buffer, std::size_t size) override;
    void seek (std::ios::off_type offset, std::ios::seekdir origin) override;
    std::ios::pos_type tell () const override;
    std::size_t size () const override;

private:

    std::unique_ptr<std::ifstream> file;
    std::size_t size_;
};

} // namespace kx
