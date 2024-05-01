#include <cstring>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <vector>
#include <mutex>

#include <cerrno>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/uio.h>

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>

#include <fmt/core.h>
#include <fmt/ostream.h>


std::mutex logfile_mutex;
std::ofstream logfile = std::ofstream("logfile.log", std::ios::app);


static int fill_dir_plus = 0;

static void* pass_init(struct fuse_conn_info* conn, struct fuse_config* cfg)
{
	std::lock_guard<std::mutex> guard(logfile_mutex);
	fmt::print(logfile, "pass_init()\n");

	(void)conn;
	cfg->use_ino = 1;
	cfg->parallel_direct_writes = 1;
	cfg->attr_timeout = 0;
	cfg->entry_timeout = 0;
	cfg->negative_timeout = 0;
	return nullptr;
}

static int pass_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi)
{
	std::lock_guard<std::mutex> guard(logfile_mutex);
	fmt::print(logfile, "pass_getattr(): {}\n", path);

	int result = 0;

	result = lstat(path, stbuf);
	if (result == -1)
		return -errno;

	return 0;
}

static int pass_statfs(const char* path, struct statvfs* stbuf)
{
	std::lock_guard<std::mutex> guard(logfile_mutex);
	fmt::print(logfile, "pass_statfs(): stubbed\n");
	return -1;
}

static int pass_access(const char* path, int mask)
{
	std::lock_guard<std::mutex> guard(logfile_mutex);
	fmt::print(logfile, "pass_access() called: {}\n", path);

	//auto res = std::filesystem::status(path).permissions();
	//if (res == std::filesystem::perms::none) return -1;
	int result = access(path, mask);
	if (result == -1)
		return -errno;

	return 0;
}
/*
static int pass_opendir(const char* path, fuse_file_info* fi)
{
	fmt::print(logfile, "pass_opendir() called: {}\n", path);

	int retstat = 0;
	DIR* dp;
	struct dirent* de;

	dp = opendir(path);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp) != NULL)
	{

	}

	return retstat;
}
*/
static int pass_truncate(const char* path, off_t size, struct fuse_file_info* fi)
{
	std::lock_guard<std::mutex> guard(logfile_mutex);
	fmt::print(logfile, "pass_truncate() called: {}\n", path);

	int result;

	if (fi != NULL)
		result = ftruncate(fi->fh, size);
	else
		result = truncate(path, size);

	if (result == -1)
		return -errno;

	return 0;
}

static int pass_open(const char* path, struct fuse_file_info* fi)
{
	std::lock_guard<std::mutex> guard(logfile_mutex);
	fmt::print(logfile, "pass_open() called: {}\n", path);

	int res = open(path, fi->flags);
	if (res == -1)
		return -errno;

	if (fi->flags & O_DIRECT)
	{
		fi->direct_io = 1;
		fi->parallel_direct_writes = 1;
	}

	fi->fh = res;
	return 0;
}

static int pass_readdir(const char* path, void* buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags)
{
	std::lock_guard<std::mutex> guard(logfile_mutex);
	fmt::print(logfile, "pass_readdir() called: {}\n", path);

	DIR* dp;
	struct dirent* de;

	(void)offset;
	(void)fi;
	(void)flags;

	dp = opendir(path);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL)
	{
		struct stat st;
		std::memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buffer, de->d_name, &st, 0, (fuse_fill_dir_flags)fill_dir_plus))
			break;
	}

	closedir(dp);
	return 0;
}

static int pass_read(const char* path, char* buffer, size_t size, off_t offset, struct fuse_file_info* fi)
{
	std::lock_guard<std::mutex> guard(logfile_mutex);
	fmt::print(logfile, "pass_read() called: {}\n", path);

	int result;
	int file_desc;

	// Open the file for reading, otherwise default to the file descriptor saved in the fuse_file_info struct
	if (fi == NULL)
		file_desc = open(path, O_RDONLY);
	else
		file_desc = fi->fh;

	// If both the requested file and the file descriptor from 'fi' are invalid, error out
	if (file_desc == -1)
		return -errno;

	// Read file from descriptor into a buffer of size, starting from an offset
	result = pread(file_desc, buffer, size, offset);
	// If the read failed for any reason, error out
	if (result == -1)
		result = -errno;

	// If the 'fi' struct has been invalidated for any reason, close the file descriptor
	if (fi == NULL)
		close(file_desc);

	return result;
}

struct fuse_operations pass_oper = {
	.getattr = pass_getattr,
	.truncate = pass_truncate,
	.open = pass_open,
	.read = pass_read,
	.statfs = pass_statfs,
	.readdir = pass_readdir,
	.init = pass_init,
	.access = pass_access
};

int main(int argc, char* argv[])
{
	// Get and push all the arguments into a vector of strings
	// TODO: Figure out whether this is actually needed and maybe merge it with the below operation
	std::vector<std::string> arguments = {};
	for (std::size_t arg_it = 1; arg_it < argc; arg_it++)
		arguments.push_back(argv[arg_it]);

	//umask(0);

	// TODO: Instead of iterating over everything into a new vector, use Block's idea of changing the first byte of a string to a null terminator

	std::vector<char*> new_args = {};
	for (auto& str : arguments)
	{
		if (str == "--plus")
			fill_dir_plus = FUSE_FILL_DIR_PLUS;
		else
			new_args.push_back(str.data());
	}

	return fuse_main(new_args.size(), new_args.data(), &pass_oper, NULL);
}
