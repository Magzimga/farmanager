#pragma once

/*
common.hpp

Some useful classes, templates && macros.

*/
/*
Copyright � 2013 Far Group
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// TODO: use separately where required

#include "common/algorithm.hpp"
#include "common/as_string.hpp"
#include "common/enum.hpp"
#include "common/enumerator.hpp"
#include "common/function_traits.hpp"
#include "common/iterator_range.hpp"
#include "common/make_vector.hpp"
#include "common/noncopyable.hpp"
#include "common/preprocessor.hpp"
#include "common/range_for.hpp"
#include "common/scope_exit.hpp"
#include "common/smart_ptr.hpp"


// TODO: clean up & split

template<int id>
struct write_t
{
	write_t(const std::wstring& str, size_t n) : m_part(str.substr(0, n)), m_size(n) {}
	std::wstring m_part;
	size_t m_size;
};

typedef write_t<0> write_max;
typedef write_t<1> write_exact;

inline std::wostream& operator <<(std::wostream& stream, const write_max& p)
{
	return stream << p.m_part;
}

inline std::wostream& operator <<(std::wostream& stream, const write_exact& p)
{
	stream.width(p.m_size);
	return stream << p.m_part;
}

template<class T>
inline void resize_nomove(T& container, size_t size)
{
	T(size).swap(container);
}

template<class T>
inline void clear_and_shrink(T& container)
{
	T().swap(container);
}

template <typename T>
bool CheckNullOrStructSize(const T* s) {return !s || (s->StructSize >= sizeof(T));}
template <typename T>
bool CheckStructSize(const T* s) {return s && (s->StructSize >= sizeof(T));}

template <typename type_1, typename type_2>
struct simple_pair
{
	typedef type_1 first_type;
	typedef type_2 second_type;

	first_type first;
	second_type second;
};

template<class T>
typename std::make_unsigned<T>::type as_unsigned(T t) { return static_cast<typename std::make_unsigned<T>::type>(t); }

template<typename T>
inline void ClearStruct(T& s)
{
	static_assert(!std::is_pointer<T>::value, "This template requires a reference to an object");
	static_assert(std::is_pod<T>::value, "This template requires a POD type");
	memset(&s, 0, sizeof(s));
}

template<typename T>
inline void ClearArray(T& a)
{
	static_assert(std::is_array<T>::value, "This template requires an array");
	static_assert(std::is_pod<T>::value, "This template requires a POD type");
	memset(a, 0, sizeof(a));
}

template<class T>
inline const T* NullToEmpty(const T* Str) { static const T empty = T(); return Str? Str : &empty; }
template<class T>
inline const T* EmptyToNull(const T* Str) { return (Str && !*Str)? nullptr : Str; }

template<class T>
inline size_t make_hash(const T& value)
{
	return std::hash<T>()(value);
}

template <class T>
inline const T Round(const T &a, const T &b) { return a / b + (a%b * 2 > b ? 1 : 0); }

inline void* ToPtr(intptr_t T){ return reinterpret_cast<void*>(T); }

template<class T>
bool InRange(const T& from, const T& what, const T& to)
{
	return from <= what && what <= to;
};

template<class owner, typename acquire, typename release>
class raii_wrapper: ::noncopyable
{
public:
	raii_wrapper(const owner& Owner, const acquire& Acquire, const release& Release): m_Owner(Owner), m_Release(Release) { (*m_Owner.*Acquire)(); }
	raii_wrapper(raii_wrapper&& rhs) noexcept: m_Release() { *this = std::move(rhs); }
	~raii_wrapper() { (*m_Owner.*m_Release)(); }
	MOVE_OPERATOR_BY_SWAP(raii_wrapper);
	void swap(raii_wrapper& rhs) noexcept { using std::swap; swap(m_Owner, rhs.m_Owner); swap(m_Release, rhs.m_Release); }
	FREE_SWAP(raii_wrapper);

private:
	owner m_Owner;
	release m_Release;
};

#ifdef _DEBUG
#define SELF_TEST(code) \
namespace \
{ \
	struct SelfTest \
	{ \
		SelfTest() \
		{ \
			code; \
		} \
	} _SelfTest; \
}
#else
#define SELF_TEST(code)
#endif

#define BIT(number) (1 << (number))

#define SIGN_UNICODE    0xFEFF
#define SIGN_REVERSEBOM 0xFFFE
#define SIGN_UTF8       0xBFBBEF
#define EOL_STR L"\r\n"

typedef std::wstring string;
