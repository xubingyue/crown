/*
Copyright (c) 2013 Daniele Bartolini, Michele Rossi
Copyright (c) 2012 Daniele Bartolini, Simone Boscaratto

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once
#undef min
#undef max

#include "Types.h"

namespace crown
{

/// Facility to store global integer settings.
class IntSetting
{
public:

						IntSetting(const char* name, const char* synopsis, int32_t value, int32_t min, int32_t max);

	const char*			name() const;
	const char*			synopsis() const;

	int32_t				value() const;
	int32_t				min() const;
	int32_t				max() const;

						operator int();

	IntSetting&			operator=(const int32_t value);

public:

	/// Returns the setting @name or NULL if not found.
	static IntSetting*	find_setting(const char* name);

private:

	const char*			m_name;
	const char*			m_synopsis;

	int32_t				m_value;
	int32_t				m_min;
	int32_t				m_max;

	IntSetting*			m_next;
};

} // namespace crown
