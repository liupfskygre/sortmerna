/**
* FILE: processor.cpp
* Created: Nov 26, 2017 Sun
*/

#include "processor.hpp"

void Processor::process()
{
	int countReads = 0;
	//	std::cout << "Processor " << id << " (" << std::this_thread::get_id() << ") started" << std::endl;
	std::stringstream ss;
	ss << std::this_thread::get_id();
	printf("Processor thread %s started\n", ss.str().c_str());
	ss.str("");
	for (;;)
	{
		Read read = readQueue.pop();
		if (read.isEmpty || !read.isValid)
		{
			if (readQueue.isDone()) break;
			else continue;
		}

		// search the forward and/or reverse strands depending on Run options
		int32_t strandCount = 0;
		index.opts.forward = true;
		if (index.opts.forward ^ index.opts.reverse) strandCount = 1; // only search the forward xor reverse strand
		else strandCount = 2; // search both strands
		for (int32_t strand = 0; strand < strandCount; strand++)
		{
			if ((strandCount == 1 && index.opts.reverse) || strand == 1)
				read.revIntStr(); // reverse the sequence
			callback(index, refs, output, readstats, read);
		}
		if (read.isValid)
			writeQueue.push(read);

		countReads++;
	}
	writeQueue.mDoneAdding();
	//	std::cout << "Processor " << id << " (" << std::this_thread::get_id() << ") done. Processed " << count << std::endl;
	ss << std::this_thread::get_id();
	printf("Processor thread %s done. Processed %d reads\n", ss.str().c_str(), countReads);
} // ~Processor::process