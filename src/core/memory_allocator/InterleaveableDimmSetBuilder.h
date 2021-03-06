/*
 * Copyright (c) 2016, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Intel Corporation nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/*
 * A mechanism to build the set of DIMMs whose capacity can be interleaved together
 * from a list of DIMMs on the same socket.
 */

#ifndef INTERLEAVEABLEDIMMSETBUILDER_H_
#define INTERLEAVEABLEDIMMSETBUILDER_H_

#include <nvm_types.h>
#include <vector>
#include <exception>
#include "MemoryAllocationTypes.h"
#include <core/ExportCore.h>

namespace core
{
namespace memory_allocator
{

class NVM_CORE_API InterleaveableDimmSetBuilder
{
	public:
		InterleaveableDimmSetBuilder();
		virtual ~InterleaveableDimmSetBuilder();

		class InvalidDimmsException : public std::exception {};

		virtual void setDimms(const std::vector<Dimm> &dimms);
		virtual std::vector<Dimm> getLargestSetOfInterleavableDimms();

	private:
		std::vector<Dimm> m_dimms;

		void validateDimmList();
		bool dimmsAreOnMultipleSockets();
		bool dimmsHaveValidChannelIds();

		std::vector<Dimm> getDimmsFromListMatchingInterleaveSet(const int interleaveBitmap);
};

} /* namespace memory_allocator */
} /* namespace core */

#endif /* INTERLEAVEABLEDIMMSETBUILDER_H_ */
