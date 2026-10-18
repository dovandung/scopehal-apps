/***********************************************************************************************************************
*                                                                                                                      *
* libsigrok4DSL plugin for ngscopeclient                                                                               *
*                                                                                                                      *
* Licensed under the GNU GPL v3 or later (libsigrok4DSL is GPLv3).                                                     *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@brief Packed logic samples to run-length (sparse) form
 */

#ifndef PackedBits_h
#define PackedBits_h

#include <cstddef>
#include <cstdint>

/**
	@brief Number of entries DecodePackedBits() will produce for this buffer

	Lets the caller allocate exactly instead of for the worst case (one entry per sample), which
	for a deep multi-channel capture would be tens of gigabytes.
 */
inline size_t CountPackedBitRuns(const uint8_t* buf, size_t nbytes)
{
	if(nbytes == 0)
		return 0;

	//Each transition between consecutive samples starts a new entry
	size_t transitions = 0;
	unsigned prev = buf[0] & 1;
	for(size_t i=0; i<nbytes; i++)
	{
		unsigned b = buf[i];
		unsigned shifted = ((b << 1) | prev) & 0xff;	//bit j = sample before bit j of b
		transitions += __builtin_popcount(b ^ shifted);
		prev = b >> 7;
	}

	//The final sample is always its own entry, even without a transition
	const size_t nbits = nbytes * 8;
	bool lastBit = (buf[nbytes-1] >> 7) & 1;
	bool prevBit = (nbits >= 2) ? ((buf[(nbits-2)/8] >> ((nbits-2)%8)) & 1) : !lastBit;
	size_t forced = (nbits >= 2 && lastBit == prevBit) ? 1 : 0;

	return 1 + transitions + forced;
}

/**
	@brief Converts packed logic samples (8 per byte, least significant bit first) to sparse form

	Consecutive equal samples are merged into one entry of matching duration. The final sample
	always starts a new entry so the waveform visibly spans the whole capture.

	The outputs must hold at least CountPackedBitRuns(buf, nbytes) elements.

	@param buf		Packed samples
	@param nbytes	Number of bytes in buf (nbytes*8 samples)
	@param shift	Added to every offset (moves the trigger to where the client expects it)
	@param offsets	Output: start of each entry, in samples
	@param durations	Output: length of each entry, in samples
	@param samples	Output: logic level of each entry

	@return Number of entries written
 */
template<typename OFF, typename DUR, typename SMP>
size_t DecodePackedBits(const uint8_t* buf, size_t nbytes, int64_t shift, OFF& offsets, DUR& durations, SMP& samples)
{
	const size_t nbits = nbytes * 8;
	if(nbits == 0)
		return 0;

	bool last = buf[0] & 1;
	size_t k = 0;
	offsets[0] = shift;
	durations[0] = 1;
	samples[0] = last;

	size_t i = 1;
	while(i < nbits)
	{
		//Fast path: a whole byte equal to the current level just extends the current entry,
		//as long as it does not contain the final sample
		if( (i % 8 == 0) && (i + 8 < nbits) )
		{
			uint8_t b = buf[i / 8];
			if( (last && b == 0xff) || (!last && b == 0x00) )
			{
				durations[k] += 8;
				i += 8;
				continue;
			}
		}

		bool s = (buf[i / 8] >> (i % 8)) & 1;
		if( (s == last) && (i + 1 < nbits) )
			durations[k] ++;
		else
		{
			k++;
			offsets[k] = shift + (int64_t)i;
			durations[k] = 1;
			samples[k] = s;
			last = s;
		}
		i++;
	}

	return k + 1;
}

#endif
