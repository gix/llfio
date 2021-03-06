/* A filing system handle
(C) 2017 Niall Douglas <http://www.nedproductions.biz/> (20 commits)
File Created: Aug 2017


Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License in the accompanying file
Licence.txt or at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.


Distributed under the Boost Software License, Version 1.0.
    (See accompanying file Licence.txt or copy at
          http://www.boost.org/LICENSE_1_0.txt)
*/

#ifndef LLFIO_FS_HANDLE_H
#define LLFIO_FS_HANDLE_H

#include "path_handle.hpp"
#include "path_view.hpp"

#include "quickcpplib/uint128.hpp"

//! \file fs_handle.hpp Provides fs_handle

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)  // dll interface
#endif

LLFIO_V2_NAMESPACE_EXPORT_BEGIN

/*! \class fs_handle
\brief A handle to something with a device and inode number.

\sa `algorithm::cached_parent_handle_adapter<T>`
*/
class LLFIO_DECL fs_handle
{
public:
  using dev_t = uint64_t;
  using ino_t = uint64_t;
  //! The path view type used by this handle
  using path_view_type = path_view;
  //! The unique identifier type used by this handle
  using unique_id_type = QUICKCPPLIB_NAMESPACE::integers128::uint128;

protected:
  mutable dev_t _devid{0};
  mutable ino_t _inode{0};

  //! Fill in _devid and _inode from the handle via fstat()
  LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<void> _fetch_inode() const noexcept;

  virtual const handle &_get_handle() const noexcept = 0;

protected:
  //! Default constructor
  constexpr fs_handle() {}  // NOLINT
  ~fs_handle() = default;
  //! Construct a handle
  constexpr fs_handle(dev_t devid, ino_t inode)
      : _devid(devid)
      , _inode(inode)
  {
  }
  //! Implicit move construction of fs_handle permitted
  constexpr fs_handle(fs_handle &&o) noexcept
      : _devid(o._devid)
      , _inode(o._inode)
  {
    o._devid = 0;
    o._inode = 0;
  }
  //! Move assignment of fs_handle permitted
  fs_handle &operator=(fs_handle &&o) noexcept
  {
    _devid = o._devid;
    _inode = o._inode;
    o._devid = 0;
    o._inode = 0;
    return *this;
  }

public:
  //! No copy construction (use `clone()`)
  fs_handle(const fs_handle &) = delete;
  //! No copy assignment
  fs_handle &operator=(const fs_handle &o) = delete;

  //! Unless `flag::disable_safety_unlinks` is set, the device id of the file when opened
  dev_t st_dev() const noexcept
  {
    if(_devid == 0 && _inode == 0)
    {
      (void) _fetch_inode();
    }
    return _devid;
  }
  //! Unless `flag::disable_safety_unlinks` is set, the inode of the file when opened. When combined with st_dev(), forms a unique identifer on this system
  ino_t st_ino() const noexcept
  {
    if(_devid == 0 && _inode == 0)
    {
      (void) _fetch_inode();
    }
    return _inode;
  }
  //! A unique identifier for this handle across the entire system. Can be used in hash tables etc.
  unique_id_type unique_id() const noexcept
  {
    if(_devid == 0 && _inode == 0)
    {
      (void) _fetch_inode();
    }
    unique_id_type ret;
    ret.as_longlongs[0] = _devid;
    ret.as_longlongs[1] = _inode;
    return ret;
  }

  /*! Obtain a handle to the path **currently** containing this handle's file entry.

  \warning This call is \b racy and can result in the wrong path handle being returned. Note that
  unless `flag::disable_safety_unlinks` is set, this implementation opens a
  `path_handle` to the source containing directory, then checks if the file entry within has the
  same inode as the open file handle. It will retry this matching until
  success until the deadline given.

  \mallocs Calls `current_path()` and thus is both expensive and calls malloc many times.

  \sa `algorithm::cached_parent_handle_adapter<T>` which overrides this with a zero cost
  implementation, thus making unlinking and relinking very considerably quicker.
  */
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<path_handle> parent_path_handle(deadline d = std::chrono::seconds(30)) const noexcept;

  LLFIO_DEADLINE_TRY_FOR_UNTIL(parent_path_handle)

  /*! Relinks the current path of this open handle to the new path specified. If `atomic_replace` is
  true, the relink \b atomically and silently replaces any item at the new path specified. This
  operation is both atomic and silent matching POSIX behaviour even on Microsoft Windows where
  no Win32 API can match POSIX semantics.

  If the handle refers to a pipe, on Microsoft Windows the base path handle is ignored as there is
  a single global named pipe namespace. Unless the path fragment begins with `\`, the string `\??\`
  is prefixed to the name before passing it to the NT kernel API which performs the rename. This
  is because `\\.\` in Win32 maps onto `\??\` in the NT kernel.

  \warning Some operating systems provide a race free syscall for renaming an open handle (Windows).
  On all other operating systems this call is \b racy and can result in the wrong file entry being
  relinked. Note that unless `flag::disable_safety_unlinks` is set, this implementation opens a
  `path_handle` to the source containing directory first, then checks before relinking that the item
  about to be relinked has the same inode as the open file handle. It will retry this matching until
  success until the deadline given. This should prevent most unmalicious accidental loss of data.

  \param base Base for any relative path.
  \param path The relative or absolute new path to relink to.
  \param atomic_replace Atomically replace the destination if a file entry already is present there.
  Choosing false for this will fail if a file entry is already present at the destination, and may
  not be an atomic operation on some platforms (i.e. both the old and new names may be linked to the
  same inode for a very short period of time). Windows and recent Linuxes are always atomic.
  \param d The deadline by which the matching of the containing directory to the open handle's inode
  must succeed, else `errc::timed_out` will be returned.
  \mallocs Except on platforms with race free syscalls for renaming open handles (Windows), calls
  `current_path()` via `parent_path_handle()` and thus is both expensive and calls malloc many times.
  */
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC
  result<void> relink(const path_handle &base, path_view_type path, bool atomic_replace = true, deadline d = std::chrono::seconds(30)) noexcept;

  LLFIO_DEADLINE_TRY_FOR_UNTIL(relink)

#if 0  // Not implemented yet for absolutely no good reason
  /*! Links the inode referred to by this open handle to the path specified. The current path
  of this open handle is not changed, unless it has no current path due to being unlinked.

  \warning Some operating systems provide a race free syscall for linking an open handle to a new
  location (Linux, Windows).
  On all other operating systems this call is \b racy and can result in the wrong inode being
  linked. Note that unless `flag::disable_safety_unlinks` is set, this implementation opens a
  `path_handle` to the source containing directory first, then checks before linking that the item
  about to be hard linked has the same inode as the open file handle. It will retry this matching
  until success until the deadline given. This should prevent most unmalicious accidental loss of
  data.

  \param base Base for any relative path.
  \param path The relative or absolute new path to hard link to.
  \param d The deadline by which the matching of the containing directory to the open handle's inode
  must succeed, else `errc::timed_out` will be returned.
  \mallocs Except on platforms with race free syscalls for renaming open handles (Windows), calls
  `current_path()` via `parent_path_handle()` and thus is both expensive and calls malloc many times.
  */
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC
  result<void> link(const path_handle &base, path_view_type path, deadline d = std::chrono::seconds(30)) noexcept;

  LLFIO_DEADLINE_TRY_FOR_UNTIL(link)

  /*! Clones the inode referenced by the open handle into a new inode referencing the same extents
  for the file content, with a copy of the same metadata, apart from ownership which is for the
  current user. Changes to either inode are guaranteed to not be seen by the other inode i.e. they
  become completely independent, even though they initially reference the same file content extents.
  If your filing system supports this call (at the time of writing - Linux: XFS, btrfs, ocfs2, smbfs;
  Mac OS: APFS; Windows: ReFS, CIFS), this is a very cheap way of copying even very large files.
  Be aware that on Samba/CIFS, rather than erroring out if the storage filesystem doesn't implement
  support, this call is implemented by having the remove machine perform the file
  copy, which is usually much faster than doing the copy over the network.

  \return A file handle instance referring to the cloned inode.
  \param base Base for any relative path.
  \param path The relative or absolute new path to clone the inode to.
  */
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC
  result<file_handle> clone_inode(const path_handle &base,
                          path_view_type path) noexcept;

  LLFIO_DEADLINE_TRY_FOR_UNTIL(clone_inode)
#endif

  /*! Unlinks the current path of this open handle, causing its entry to immediately disappear
  from the filing system. On Windows before Windows 10 1709 unless
  `flag::win_disable_unlink_emulation` is set, this behaviour is simulated by renaming the file
  to something random and setting its delete-on-last-close flag. Note that Windows may prevent
  the renaming of a file in use by another process, if so it will NOT be renamed.
  After the next handle to that file closes, it will become permanently unopenable by anyone
  else until the last handle is closed, whereupon the entry will be eventually removed by the
  operating system.

  \warning Some operating systems provide a race free syscall for unlinking an open handle (Windows).
  On all other operating systems this call is \b racy and can result in the wrong file entry being
  unlinked. Note that unless `flag::disable_safety_unlinks` is set, this implementation opens a
  `path_handle` to the containing directory first, then checks that the item about to be unlinked
  has the same inode as the open file handle. It will retry this matching until success until the
  deadline given. This should prevent most unmalicious accidental loss of data.

  \param d The deadline by which the matching of the containing directory to the open handle's inode
  must succeed, else `errc::timed_out` will be returned.
  \mallocs Except on platforms with race free syscalls for unlinking open handles (Windows), calls
  `current_path()` and thus is both expensive and calls malloc many times. On Windows, also calls
  `current_path()` if `flag::disable_safety_unlinks` is not set.
  */
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC
  result<void> unlink(deadline d = std::chrono::seconds(30)) noexcept;

  LLFIO_DEADLINE_TRY_FOR_UNTIL(unlink)
};

namespace detail
{
  LLFIO_HEADERS_ONLY_FUNC_SPEC result<path_handle> containing_directory(optional<std::reference_wrapper<filesystem::path>> out_filename, const handle &h, const fs_handle &fsh, deadline d) noexcept;
}

// BEGIN make_free_functions.py
/*! Relinks the current path of this open handle to the new path specified. If `atomic_replace` is
true, the relink \b atomically and silently replaces any item at the new path specified. This operation
is both atomic and silent matching POSIX behaviour even on Microsoft Windows where
no Win32 API can match POSIX semantics.

\warning Some operating systems provide a race free syscall for renaming an open handle (Windows).
On all other operating systems this call is \b racy and can result in the wrong file entry being
relinked. Note that unless `flag::disable_safety_unlinks` is set, this implementation opens a
`path_handle` to the source containing directory first, then checks before relinking that the item
about to be relinked has the same inode as the open file handle. It will retry this matching until
success until the deadline given. This should prevent most unmalicious accidental loss of data.

\param self The object whose member function to call.
\param base Base for any relative path.
\param path The relative or absolute new path to relink to.
\param atomic_replace Atomically replace the destination if a file entry already is present there.
Choosing false for this will fail if a file entry is already present at the destination, and may
not be an atomic operation on some platforms (i.e. both the old and new names may be linked to the
same inode for a very short period of time). Windows and recent Linuxes are always atomic.
\param d The deadline by which the matching of the containing directory to the open handle's inode
must succeed, else `errc::timed_out` will be returned.
\mallocs Except on platforms with race free syscalls for renaming open handles (Windows), calls
`current_path()` via `parent_path_handle()` and thus is both expensive and calls malloc many times.
*/
inline result<void> relink(fs_handle &self, const path_handle &base, fs_handle::path_view_type path, bool atomic_replace = true, deadline d = std::chrono::seconds(30)) noexcept
{
  return self.relink(std::forward<decltype(base)>(base), std::forward<decltype(path)>(path), std::forward<decltype(atomic_replace)>(atomic_replace), std::forward<decltype(d)>(d));
}
/*! Unlinks the current path of this open handle, causing its entry to immediately disappear from the filing system.
On Windows unless `flag::win_disable_unlink_emulation` is set, this behaviour is
simulated by renaming the file to something random and setting its delete-on-last-close flag.
Note that Windows may prevent the renaming of a file in use by another process, if so it will
NOT be renamed.
After the next handle to that file closes, it will become permanently unopenable by anyone
else until the last handle is closed, whereupon the entry will be eventually removed by the
operating system.

\warning Some operating systems provide a race free syscall for unlinking an open handle (Windows).
On all other operating systems this call is \b racy and can result in the wrong file entry being
unlinked. Note that unless `flag::disable_safety_unlinks` is set, this implementation opens a
`path_handle` to the containing directory first, then checks that the item about to be unlinked
has the same inode as the open file handle. It will retry this matching until success until the
deadline given. This should prevent most unmalicious accidental loss of data.

\param self The object whose member function to call.
\param d The deadline by which the matching of the containing directory to the open handle's inode
must succeed, else `errc::timed_out` will be returned.
\mallocs Except on platforms with race free syscalls for unlinking open handles (Windows), calls
`current_path()` and thus is both expensive and calls malloc many times. On Windows, also calls
`current_path()` if `flag::disable_safety_unlinks` is not set.
*/
inline result<void> unlink(fs_handle &self, deadline d = std::chrono::seconds(30)) noexcept
{
  return self.unlink(std::forward<decltype(d)>(d));
}
// END make_free_functions.py

LLFIO_V2_NAMESPACE_END

#if LLFIO_HEADERS_ONLY == 1 && !defined(DOXYGEN_SHOULD_SKIP_THIS)
#define LLFIO_INCLUDED_BY_HEADER 1
#ifdef _WIN32
#include "detail/impl/windows/fs_handle.ipp"
#else
#include "detail/impl/posix/fs_handle.ipp"
#endif
#undef LLFIO_INCLUDED_BY_HEADER
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
