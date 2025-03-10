// Copyright © 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016,
//             2017, 2018, 2019, 2020, 2021, 2022, 2023, 2024
//           Vladimír Vondruš <mosra@centrum.cz> and contributors
// Copyright © 2020-2024 Dan R.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#pragma once

#include "../Common.h"

#ifndef DOXYGEN_GENERATING_OUTPUT

namespace Death { namespace Containers { namespace Implementation {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	template<std::size_t ...> struct Sequence {};

	template<class A, class B> struct SequenceConcat;
	template<std::size_t ...first, std::size_t ...second> struct SequenceConcat<Sequence<first...>, Sequence<second...>> {
		typedef Sequence<first..., (sizeof...(first) + second)...> Type;
	};

	template<std::size_t N> struct GenerateSequence :
		SequenceConcat<typename GenerateSequence<N/2>::Type,
					   typename GenerateSequence<N - N/2>::Type> {};
	template<> struct GenerateSequence<1> { typedef Sequence<0> Type; };
	template<> struct GenerateSequence<0> { typedef Sequence<> Type; };

}}}

#endif