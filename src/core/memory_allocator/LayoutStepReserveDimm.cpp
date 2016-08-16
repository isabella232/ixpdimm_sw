/*
 * Copyright (c) 2015 2016, Intel Corporation
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
 * Lay out the reserve dimm in memory.
 */

#include "LayoutStepReserveDimm.h"

#include <utility.h>
#include <LogEnterExit.h>
#include <core/exceptions/NvmExceptionBadRequest.h>
#include "ReserveDimmSelector.h"

core::memory_allocator::LayoutStepReserveDimm::LayoutStepReserveDimm()
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
}

core::memory_allocator::LayoutStepReserveDimm::~LayoutStepReserveDimm()
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
}

void core::memory_allocator::LayoutStepReserveDimm::execute(const MemoryAllocationRequest& request,
		MemoryAllocationLayout& layout)
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);

	if (request.reserveDimm)
	{
		try
		{
			Dimm reservedDimm = getReserveDimm(request.dimms);
			setReserveDimmForStorage(reservedDimm, layout);
		}
		catch (ReserveDimmSelector::NoDimmsException &)
		{
			throw core::NvmExceptionBadRequestNoDimms();
		}
	}
}

core::memory_allocator::Dimm core::memory_allocator::LayoutStepReserveDimm::getReserveDimm(
		const std::vector<Dimm>& dimms)
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);

	ReserveDimmSelector selector(dimms);
	std::string reservedDimmUid = selector.getReservedDimm();

	Dimm reserveDimm = dimms.front();
	for (std::vector<Dimm>::const_iterator dimmIter = dimms.begin(); dimmIter != dimms.end(); dimmIter++)
	{
		if (dimmIter->uid == reservedDimmUid)
		{
			reserveDimm = *dimmIter;
			break;
		}
	}

	return reserveDimm;
}

void core::memory_allocator::LayoutStepReserveDimm::setReserveDimmForStorage(struct Dimm reserveDimm,
		MemoryAllocationLayout& layout)
{
	layout.storageCapacity += reserveDimm.capacity / BYTES_PER_GB;
	layout.goals[reserveDimm.uid].memory_size = 0;
	layout.goals[reserveDimm.uid].app_direct_count = 0;
	layout.reservedimmUid = reserveDimm.uid;
}

