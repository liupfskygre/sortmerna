/**
* FILE: output.cpp
* Created: Nov 26, 2017 Sun
*/
#include "output.hpp"
#include "ThreadPool.hpp"
#include "kvdb.hpp"
#include "readsqueue.hpp"
#include "index.hpp"
#include "references.hpp"
#include "reader.hpp"
#include "processor.hpp"


// forward
void writeAlignmentJob(Index & index, References & refs, Output & output, Readstats & readstats, Read & read); // callback

void Output::init(Readstats & readstats)
{
	// attach pid to output files
	char pidStr[4000];
	if (pid_gv)
	{
		int32_t pid = getpid();
		sprintf(pidStr, "%d", pid);
	}

	// associate the streams with reference sequence file names
	if (opts.ptr_filetype_ar != NULL)
	{
		if (fastxout_gv)
		{
			// fasta/fastq output
			acceptedstrings.assign(opts.ptr_filetype_ar);
			if (pid_gv)
			{
				acceptedstrings.append("_");
				acceptedstrings.append(pidStr);
			}
			acceptedstrings.append(".");
			acceptedstrings.append(readstats.suffix.c_str());

			acceptedreads.open(acceptedstrings);
			acceptedreads.close();
		}

		if (opts.samout)
		{
			// sam output
			acceptedstrings_sam.assign(opts.ptr_filetype_ar);
			if (pid_gv)
			{
				acceptedstrings_sam.append("_");
				acceptedstrings_sam.append(pidStr);
			}
			acceptedstrings_sam.append(".sam");
			acceptedsam.open(acceptedstrings_sam);
			acceptedsam.close();
		}

		if (opts.blastout)
		{
			// blast output
			acceptedstrings_blast.assign(opts.ptr_filetype_ar);
			if (pid_gv)
			{
				acceptedstrings_blast.append("_");
				acceptedstrings_blast.append(pidStr);
			}
			acceptedstrings_blast.append(".blast");
			acceptedblast.open(acceptedstrings_blast);
			acceptedblast.close();
		}

		if (logout_gv)
		{
			// statistics file output
			ofstream logstream;
			logoutfile.assign(opts.ptr_filetype_ar);
			if (pid_gv)
			{
				logoutfile.append("_");
				logoutfile.append(pidStr);
			}
			logoutfile.append(".log");

			logstream.open(logoutfile);
			logstream.close();
		}

		if (otumapout_gv)
		{
			// OTU map output file
			ofstream otumap;
			acceptedotumap_file.assign(opts.ptr_filetype_ar);
			if (pid_gv)
			{
				acceptedotumap_file.append("_");
				acceptedotumap_file.append(pidStr);
			}
			acceptedotumap_file.append("_otus.txt");
			otumap.open(acceptedotumap_file);
			otumap.close();
		}

		if (de_novo_otu_gv)
		{
			ofstream denovo_otu;
			denovo_otus_file.assign(opts.ptr_filetype_ar);
			if (pid_gv)
			{
				denovo_otus_file.append("_");
				denovo_otus_file.append(pidStr);
			}
			denovo_otus_file.append("_denovo.");
			denovo_otus_file.append(readstats.suffix.c_str());

			denovo_otu.open(denovo_otus_file);
			denovo_otu.close();
		}
	}//~if ( ptr_filetype_ar != NULL ) 

	if (opts.ptr_filetype_or != NULL)
	{
		if (fastxout_gv)
		{
			// output stream for other reads
			ofstream otherreads;
			// add suffix database name to accepted reads file
			if (pid_gv)
			{
				strcat(opts.ptr_filetype_or, "_");
				strcat(opts.ptr_filetype_or, pidStr);
			}
			strcat(opts.ptr_filetype_or, ".");
			strcat(opts.ptr_filetype_or, readstats.suffix.c_str());
			// create the other reads file
			otherreads.open(opts.ptr_filetype_or);
			otherreads.close();
		}
	}
} // ~Output::init

  /**
  * Prototype: paralleltraversal lines 1531..1555
  * Calculate Mismatches, Gaps, and ID
  *
  * @param IN Refs  references
  * @param IN Read
  * @param IN alignIdx index into Read.hits_align_info.alignv
  * @param OUT mismatches  calculated here for the given Read Alignment
  * @param OUT gaps
  * @param OUT id
  */
void Output::calcMismatchGapId(References & refs, Read & read, int alignIdx, uint32_t & mismatches, uint32_t & gaps, double & id)
{
	const char to_char[5] = { 'A','C','G','T','N' };

	int32_t qb = read.hits_align_info.alignv[alignIdx].ref_begin1; //ptr_alignment->ref_begin1;
	int32_t pb = read.hits_align_info.alignv[alignIdx].read_begin1; //->read_begin1;

	std::string refseq = refs.buffer[read.hits_align_info.alignv[alignIdx].ref_seq].sequence;

	for (uint32_t c2 = 0; c2 < read.hits_align_info.alignv[alignIdx].cigar.size(); ++c2)
	{
		uint32_t letter = 0xf & read.hits_align_info.alignv[alignIdx].cigar[c2]; // 4 low bits
		uint32_t length = (0xfffffff0 & read.hits_align_info.alignv[alignIdx].cigar[c2]) >> 4; // high 28 bits i.e. 32-4=28
		if (letter == 0)
		{
			for (uint32_t u = 0; u < length; ++u)
			{
				if ((char)to_char[(int)refseq[qb]] != (char)to_char[(int)read.sequence[pb]]) ++mismatches;
				else ++id;
				++qb;
				++pb;
			}
		}
		else if (letter == 1)
		{
			pb += length;
			gaps += length;
		}
		else
		{
			qb += length;
			gaps += length;
		}
	}
} // ~Output::calcMismatchGapId

void Output::report_blast
(
	ofstream &fileout,
	Index & index,
	References & refs,
	Read & read
)
{
	const char to_char[5] = { 'A','C','G','T','N' };
	double id = 0;
	uint32_t mismatches = 0;
	uint32_t gaps = 0;

	// iterate all alignments of the read
	for (int i = 0; i < read.hits_align_info.alignv.size(); ++i)
	{
		uint32_t bitscore = (uint32_t)((float)((index.gumbel[index.index_num].first)
			* (read.hits_align_info.alignv[i].score1) - log(index.gumbel[index.index_num].second)) / (float)log(2));

		double evalue_score = (double)(index.gumbel[index.index_num].second) * index.full_ref[index.index_num]
			* index.full_read[index.index_num]
			* std::exp(-(index.gumbel[index.index_num].first) * read.hits_align_info.alignv[i].score1);

		std::string refseq = refs.buffer[read.hits_align_info.alignv[i].ref_seq].sequence;
		std::string refhead = refs.buffer[read.hits_align_info.alignv[i].ref_seq].header;

		// Blast-like pairwise alignment (only for aligned reads)
		if (index.opts.blastFormat == BlastFormat::REGULAR) // TODO: global - fix
		{
			fileout << "Sequence ID: ";
			//const char* tmp = read.hits_align_info.alignv[alignIdx].ref_seq; // ref_name
			//while (*tmp != '\n') fileout << *tmp++;
			fileout << refhead.substr(0, refhead.find(' ')); // print only start of the header till first space
			fileout << endl;

			fileout << "Query ID: ";
			//tmp = read_name;
			//while (*tmp != '\n') fileout << *tmp++;
			fileout << read.header.substr(0, read.header.find(' '));
			fileout << endl;

			//fileout << "Score: " << a->score1 << " bits (" << bitscore << ")\t";
			fileout << "Score: " << read.hits_align_info.alignv[i].score1 << " bits (" << bitscore << ")\t";
			fileout.precision(3);
			fileout << "Expect: " << evalue << "\t";

			if (read.hits_align_info.alignv[i].strand) fileout << "strand: +\n\n";
			else fileout << "strand: -\n\n";

			if (read.hits_align_info.alignv[i].cigar.size() > 0)
			{
				uint32_t j, c = 0, left = 0, e = 0,
					qb = read.hits_align_info.alignv[i].ref_begin1,
					pb = read.hits_align_info.alignv[i].read_begin1; //mine

				while (e < read.hits_align_info.alignv[i].cigar.size() || left > 0)
				{
					int32_t count = 0;
					int32_t q = qb;
					int32_t p = pb;
					fileout << "Target: ";
					fileout.width(8);
					fileout << q + 1 << "    ";
					// process CIGAR
					for (c = e; c < read.hits_align_info.alignv[i].cigar.size(); ++c)
					{
						// 4 Low bits encode a Letter: M | D | S
						uint32_t letter = 0xf & read.hits_align_info.alignv[i].cigar[c]; // *(a->cigar + c)
																						 // 28 High bits encode the number of occurencies e.g. 34
						uint32_t length = (0xfffffff0 & read.hits_align_info.alignv[i].cigar[c]) >> 4; // *(a->cigar + c)
						uint32_t l = (count == 0 && left > 0) ? left : length;
						for (j = 0; j < l; ++j)
						{
							if (letter == 1) fileout << "-";
							else
							{
								//fileout << to_char[(int)*(refseq + q)];
								fileout << to_char[(int)refseq[q]];
								++q;
							}
							++count;
							if (count == 60) goto step2;
						}
					}
				step2:
					fileout << "    " << q << "\n";
					fileout.width(20);
					fileout << " ";
					q = qb;
					count = 0;
					for (c = e; c < read.hits_align_info.alignv[i].cigar.size(); ++c)
					{
						//uint32_t letter = 0xf & *(a->cigar + c);
						uint32_t letter = 0xf & read.hits_align_info.alignv[i].cigar[c];
						uint32_t length = (0xfffffff0 & read.hits_align_info.alignv[i].cigar[c]) >> 4;
						uint32_t l = (count == 0 && left > 0) ? left : length;
						for (j = 0; j < l; ++j)
						{
							if (letter == 0)
							{
								if ((char)to_char[(int)refseq[q]] == (char)to_char[(int)read.sequence[p]]) fileout << "|";
								else fileout << "*";
								++q;
								++p;
							}
							else
							{
								fileout << " ";
								if (letter == 1) ++p;
								else ++q;
							}
							++count;
							if (count == 60)
							{
								qb = q;
								goto step3;
							}
						}
					}
				step3:
					p = pb;
					fileout << "\nQuery: ";
					fileout.width(9);
					fileout << p + 1 << "    ";
					count = 0;
					for (c = e; c < read.hits_align_info.alignv[i].cigar.size(); ++c)
					{
						uint32_t letter = 0xf & read.hits_align_info.alignv[i].cigar[c];
						uint32_t length = (0xfffffff0 & read.hits_align_info.alignv[i].cigar[c]) >> 4;
						uint32_t l = (count == 0 && left > 0) ? left : length;
						for (j = 0; j < l; ++j)
						{
							if (letter == 2) fileout << "-";
							else
							{
								fileout << (char)to_char[(int)read.sequence[p]];
								++p;
							}
							++count;
							if (count == 60)
							{
								pb = p;
								left = l - j - 1;
								e = (left == 0) ? (c + 1) : c;
								goto end;
							}
						}
					}
					e = c;
					left = 0;
				end:
					fileout << "    " << p << "\n\n";
				}
			}
		}
		// Blast tabular m8 + optional columns for CIGAR and query coverage
		else if (index.opts.blastFormat == BlastFormat::TABULAR)
		{
			// (1) Query
			//while ((*read_name != ' ') && (*read_name != '\n') && (*read_name != '\t')) fileout << (char)*read_name++;
			fileout << read.header.substr(0, read.header.find(' ')); // part of the header till first space

			// print null alignment for non-aligned read
			if (print_all_reads_gv && (read.hits_align_info.alignv.size() == 0))
			{
				fileout << "\t*\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0";
				for (uint32_t l = 0; l < user_opts.size(); l++)
				{
					if (user_opts[l].compare("cigar") == 0)
						fileout << "\t*";
					else if (user_opts[l].compare("qcov") == 0)
						fileout << "\t0";
					else if (user_opts[l].compare("qstrand") == 0)
						fileout << "\t*";
					fileout << "\n";
				}
				return;
			}

			Output::calcMismatchGapId(refs, read, i, mismatches, gaps, id);

			fileout << "\t";
			// (2) Subject
			//while ((*ref_name != ' ') && (*ref_name != '\n') && (*ref_name != '\t')) fileout << (char)*ref_name++;
			fileout << refhead.substr(0, refhead.find(' '));
			fileout << "\t";
			// (3) %id
			fileout.precision(3);
			fileout << id * 100 << "\t";
			// (4) alignment length
			fileout << (read.hits_align_info.alignv[i].read_end1 - read.hits_align_info.alignv[i].read_begin1 + 1) << "\t";
			// (5) mismatches
			fileout << mismatches << "\t";
			// (6) gap openings
			fileout << gaps << "\t";
			// (7) q.start
			fileout << read.hits_align_info.alignv[i].read_begin1 + 1 << "\t";
			// (8) q.end
			fileout << read.hits_align_info.alignv[i].read_end1 + 1 << "\t";
			// (9) s.start
			fileout << read.hits_align_info.alignv[i].ref_begin1 + 1 << "\t";
			// (10) s.end
			fileout << read.hits_align_info.alignv[i].ref_end1 + 1 << "\t";
			// (11) e-value
			fileout << evalue << "\t";
			// (12) bit score
			fileout << bitscore;
			// OPTIONAL columns
			for (uint32_t l = 0; l < user_opts.size(); l++)
			{
				// output CIGAR string
				if (user_opts[l].compare("cigar") == 0)
				{
					fileout << "\t";
					// masked region at beginning of alignment
					if (read.hits_align_info.alignv[i].read_begin1 != 0) fileout << read.hits_align_info.alignv[i].read_begin1 << "S";
					for (int c = 0; c < read.hits_align_info.alignv[i].cigar.size(); ++c)
					{
						uint32_t letter = 0xf & read.hits_align_info.alignv[i].cigar[c];
						uint32_t length = (0xfffffff0 & read.hits_align_info.alignv[i].cigar[c]) >> 4;
						fileout << length;
						if (letter == 0) fileout << "M";
						else if (letter == 1) fileout << "I";
						else fileout << "D";
					}

					uint32_t end_mask = read.sequence.length() - read.hits_align_info.alignv[i].read_end1 - 1;
					// output the masked region at end of alignment
					if (end_mask > 0) fileout << end_mask << "S";
				}
				// output % query coverage
				else if (user_opts[l].compare("qcov") == 0)
				{
					fileout << "\t";
					fileout.precision(3);
					double coverage = abs(read.hits_align_info.alignv[i].read_end1 - read.hits_align_info.alignv[i].read_begin1 + 1)
						/ read.hits_align_info.alignv[i].readlen;
					fileout << coverage * 100; // (double)align_len / readlen
				}
				// output strand
				else if (user_opts[l].compare("qstrand") == 0)
				{
					fileout << "\t";
					if (read.hits_align_info.alignv[i].strand) fileout << "+";
					else fileout << "-";
				}
			}
			fileout << "\n";
		}//~blast tabular m8
	} // ~iterate all alignments
} // ~ Output::report_blast


void Output::report_sam
(
	ofstream &fileout,
	References & refs,
	Read & read
)
{
	fileout << "Not implemented\n";
#if 0
	const char to_char[5] = { 'A','C','G','T','N' };
	// (1) Query
	//while ((*read_name != ' ') && (*read_name != '\n') && (*read_name != '\t')) fileout << (char)*read_name++;
	fileout << read.header.substr(0, read.header.find(' '));
	// read did not align, output null string
	if (print_all_reads_gv && (a == NULL))
	{
		fileout << "\t4\t*\t0\t0\t*\t*\t0\t0\t*\t*\n";
		return;
	}
	// read aligned, output full alignment
	uint32_t c;
	// (2) flag
	if (!strand) fileout << "\t16\t";
	else fileout << "\t0\t";
	// (3) Subject
	while ((*ref_name != ' ') && (*ref_name != '\n') && (*ref_name != '\t'))
		fileout << (char)*ref_name++;
	// (4) Ref start
	fileout << "\t" << a->ref_begin1 + 1;
	// (5) mapq
	fileout << "\t" << 255 << "\t";
	// (6) CIGAR
	// output the masked region at beginning of alignment
	if (a->read_begin1 != 0) fileout << a->read_begin1 << "S";

	for (c = 0; c < a->cigarLen; ++c) {
		uint32_t letter = 0xf & *(a->cigar + c);
		uint32_t length = (0xfffffff0 & *(a->cigar + c)) >> 4;
		fileout << length;
		if (letter == 0) fileout << "M";
		else if (letter == 1) fileout << "I";
		else fileout << "D";
	}
	uint32_t end_mask = readlen - a->read_end1 - 1;
	// output the masked region at end of alignment
	if (end_mask > 0) fileout << end_mask << "S";
	// (7) RNEXT, (8) PNEXT, (9) TLEN
	fileout << "\t*\t0\t0\t";
	// (10) SEQ
	const char* ptr_read_seq = read_seq;
	while (*ptr_read_seq != '\n') fileout << (char)to_char[(int)*ptr_read_seq++];
	// (11) QUAL
	fileout << "\t";
	// reverse-complement strand
	if (read_qual && !strand)
	{
		while (*read_qual != '\n') fileout << (char)*read_qual--;
		// forward strand
	}
	else if (read_qual) {
		while ((*read_qual != '\n') && (*read_qual != '\0')) fileout << (char)*read_qual++;
		// FASTA read
	}
	else fileout << "*";

	// (12) OPTIONAL FIELD: SW alignment score generated by aligner
	fileout << "\tAS:i:" << a->score1;
	// (13) OPTIONAL FIELD: edit distance to the reference
	fileout << "\tNM:i:" << diff << "\n";
#endif
} // ~Output::report_sam

/**
 * open streams for writing
 */
void Output::openfiles()
{
	if (opts.blastout) acceptedblast.open(acceptedstrings_blast);
}

void Output::closefiles()
{
	if (acceptedblast.is_open()) acceptedblast.close();
}

// called from main. TODO: move into a class?
void generateReports(Runopts opts)
{
	int N_READ_THREADS = 1;
	int N_PROC_THREADS = 1;
	int loopCount = 0; // counter of total number of processing iterations. TODO: no need here?
	std::stringstream ss;

	ss << "generateReports called. Thread: " << std::this_thread::get_id() << std::endl;
	std::cout << ss.str(); ss.str("");

	ThreadPool tpool(N_READ_THREADS + N_PROC_THREADS);
	KeyValueDatabase kvdb(opts.kvdbPath);
	ReadsQueue readQueue("read_queue", QUEUE_SIZE_MAX, N_READ_THREADS); // shared: Processor pops, Reader pushes
	ReadsQueue writeQueue("write_queue", QUEUE_SIZE_MAX, N_PROC_THREADS); // Not used for Reports
	Readstats readstats(opts);
	Output output(opts, readstats);
	Index index(opts, readstats, output);
	References refs(opts, index);

	output.openfiles();

	// loop through every index passed to option --ref (ex. SSU 16S and SSU 18S)
	for (uint16_t index_num = 0; index_num < (uint16_t)opts.indexfiles.size(); ++index_num)
	{
		// iterate every part of an index
		for (uint16_t idx_part = 0; idx_part < index.num_index_parts[index_num]; ++idx_part)
		{
			ss << "\tLoading index part " << idx_part+1 << "/" << index.num_index_parts[index_num] << "  ...\n";
			std:cout << ss.str(); ss.str("");
			auto t = std::chrono::high_resolution_clock::now();
			index.load(index_num, idx_part);
			refs.load(index_num, idx_part);
			std::chrono::duration<double> elapsed = std::chrono::high_resolution_clock::now() - t; // ~20 sec Debug/Win
			ss << "done [" << std::setprecision(2) << std::fixed << elapsed.count() << " sec]\n";
			std::cout << ss.str(); ss.str("");

			for (int i = 0; i < N_READ_THREADS; ++i)
			{
				tpool.addJob(Reader("reader_" + std::to_string(i), opts, readQueue, kvdb, loopCount));
			}

			// add processor jobs
			for (int i = 0; i < N_PROC_THREADS; ++i)
			{
				tpool.addJob(ReportProcessor("proc_" + std::to_string(i), readQueue, writeQueue, readstats, index, refs, output, writeAlignmentJob));
			}
			++loopCount;
		} // ~for(idx_part)
	} // ~for(index_num)
	tpool.waitAll(); // wait till processing is done
	output.closefiles();
	std::cout << "Done generateReports\n";
} // ~generateReports

// called on each read
void writeAlignmentJob(
	Index & index,
	References & refs,
	Output & output,
	Readstats & readstats,
	Read & read
)
{
	// TODO: remove this part. [jenya's message of 20171121 8:47]
	//map<uint64_t, alignment_struct >::iterator alignment = read_hits_align_info.find(readn); // 
	// this read does not have any alignment
	//if (alignment == read_hits_align_info.end())
	//if (read.hits_align_info.alignv.size() == 0)
	//{
	// (output NULL alignment only once)
	//	if ((index.index_num == 0) && (index.part == 0))
	//	{
	// do not output this read for de novo clustering
	// (it did not pass the E-value threshold)
	//		if (de_novo_otu_gv && read.hit_denovo) read.hit_denovo != read.hit_denovo; // flip read_hits_denovo[readn]
	// output null string for read alignment
	//		if (print_all_reads_gv)
	//		{
	//			s_align* null_alignment = NULL;
	//			if (blastout_gv && blast_tabular)
	//			{
	//				report_blast(
	//					output.acceptedblast, // blast output file
	//					null_alignment, // SW alignment cigar
	//					read.header.c_str(), //read name
	//					0, // read sequence (in integer format)
	//					0, // read quality
	//					0, // reference name
	//					0, // reference sequence
	//					0, // e-value score
	//					0, // read length (to compute the masked regions)
	//					0, // bitscore
	//					0, // forward or reverse strand
	//					0, // %id
	//					0, // %query coverage
	//					0, // number of mismatches
	//					0 // number of gaps
	//				);
	//			}
	//			if (samout_gv)
	//			{
	//				report_sam(
	//					output.acceptedsam, // sam output file
	//					null_alignment, // SW alignment cigar
	//					read.header.c_str(), // read name
	//					0, // read sequence (in integer format)
	//					0, // read quality
	//					0, // reference name
	//					0, // reference sequence
	//					0, // read length (to compute the masked regions)
	//					0, // forward or reverse strand
	//					0 // edit distance
	//				);
	//			}
	//		}
	//	}
	// go to next read
	//continue;
	//	return;
	//} // ~ if read has no alignment information

	// get all alignments for this read
	//s_align* ptr_alignment = alignment->second.ptr; // &read.hits_align_info.alignv[0];
	//if (ptr_alignment == NULL)
	//{
	//	fprintf(stderr, "  ERROR: s_align* ptr_alignment == NULL, this should not be possible.\n");
	//	exit(EXIT_FAILURE);
	//}

	// OTU-map: index of alignment holding maximum SW score
	uint32_t index_max_score = read.hits_align_info.max_index;
	// loop through all of the best alignments for this read
	//for (uint32_t p = 0; p < alignment->second.size; p++)
	for (uint32_t p = 0; p < read.hits_align_info.alignv.size(); ++p)
	{
		// continue loop if the reference sequence in this alignment
		// belongs to the database section currently loaded into RAM
		if ((read.hits_align_info.alignv[p].index_num == index.index_num) && (read.hits_align_info.alignv[p].part == index.part))
		{
			// format read & get read length
			//char myread[READLEN];
			uint32_t readlen = read.hits_align_info.alignv[p].readlen;
			// format forward read from char to int
			//if (read.hits_align_info.alignv[p].strand)
			//{
			//	format_forward(reads[readn], &myread[0], filesig); // read.isequence
			//}
			// format reverse-complement read from char to int
			//else
			//{
			//	char* end_read = NULL;
			// FASTA
			//	if (filesig == '>')
			//	{
			// if split-read (for file_s > 0)
			// note: the split-read contains two reads (a pair, in case reads are paired) that
			//   are located at another address space than the contiguous memory mapped
			//   section of reads
			//		if ((readn < 4) && (file_s > 0))
			//		{
			//			end_read = reads[readn];
			//			while (*end_read != '\0') end_read++;
			//			if (*end_read == '\0') end_read--; //account for '\0'
			//			if (*end_read == '\n' || *end_read == '\r') end_read--; //account for '\n'
			//		}
			// last read in the partial file section
			//		else if (readn >= (strs - 2))
			//		{
			//			end_read = reads[readn];
			// if processing last file section, the final read will end with '\0'
			//			if (file_s == file_sections - 1)
			//			{
			//				while (*end_read++ != '\0');
			// back-track 3 nucleotides (\n\0_)
			//				if (*(end_read - 2) == '\n' || *(end_read - 2) == '\r') end_read--;
			// back-track 2 nucleotides (\0_)
			//				end_read -= 2;
			//			}
			// if processing a file section > 0 and < last file section, the final read will end with '>' (beginning of split-read)
			//			else
			//			{
			//				while (*end_read++ != '>');
			// back-track 3 nucleotides (\n>_)
			//				end_read -= 3;
			//			}
			//		}
			// all other reads
			//		else end_read = reads[readn + 1] - 2;
			//	}
			// FASTQ
			//	else end_read = reads[readn] + readlen - 1;
			//	format_rev(reads[readn], end_read, &myread[0], filesig);
			////~reverse-complement read

			// get the edit distance between reference and read
			char to_char[5] = { 'A','C','G','T','N' };
			double id = 0;
			uint32_t mismatches = 0;
			uint32_t gaps = 0;

			uint32_t ref_seq = read.hits_align_info.alignv[p].ref_seq; // ptr_alignment->ref_seq;
																	   //char* ref_seq_ptr = read.hits_align_info.alignv[p].ref_seq[0]; // reference_seq[(2 * ref_seq) + 1]
																	   //char* read_seq_ptr = myread;
			int32_t qb = read.hits_align_info.alignv[p].ref_begin1; //ptr_alignment->ref_begin1;
			int32_t pb = read.hits_align_info.alignv[p].read_begin1; //->read_begin1;

			output.calcMismatchGapId(refs, read, p, mismatches, gaps, id);

			//int32_t align_len = abs(ptr_alignment->read_end1 + 1 - ptr_alignment->read_begin1);
			int32_t align_len = abs(read.hits_align_info.alignv[p].read_end1 + 1 - read.hits_align_info.alignv[p].read_begin1);
			int32_t total_pos = mismatches + gaps + id;
			stringstream ss;
			ss.precision(3);
			ss << (double)id / total_pos << ' ' << (double)align_len / read.hits_align_info.alignv[p].readlen;
			double align_id_round = 0.0;
			double align_cov_round = 0.0;
			ss >> align_id_round >> align_cov_round;

			// alignment with the highest SW score passed
			// %id and %coverage thresholds
			if ((p == index_max_score) &&
				(align_id_round >= align_id) &&
				(align_cov_round >= align_cov))
			{
				// increment number of reads passing identity
				// and coverage threshold
				readstats.total_reads_mapped_cov++;
				// do not output read for de novo OTU construction
				// (it passed the %id/coverage thresholds)
				if (de_novo_otu_gv && read.hit_denovo) read.hit_denovo = !read.hit_denovo; // flip
																						   // fill OTU map with highest-scoring alignment for the read
				if (otumapout_gv)
				{
					// reference sequence identifier for mapped read
					//char ref_seq_arr[4000] = "";
					//char* ref_seq_arr_ptr = ref_seq_arr;
					//char* ref_seq_id_ptr = reference_seq[(2 * ref_seq)] + 1;
					//while ((*ref_seq_id_ptr != ' ') && (*ref_seq_id_ptr != '\n') && (*ref_seq_id_ptr != '\r')) 
					//	*ref_seq_arr_ptr++ = *ref_seq_id_ptr++;
					//string ref_seq_str = ref_seq_arr;
					std::string refhead = refs.buffer[read.hits_align_info.alignv[p].ref_seq].header;
					string ref_seq_str = refhead.substr(0, refhead.find(' '));

					// read identifier
					//char read_seq_arr[4000] = "";
					//char* read_seq_arr_ptr = read_seq_arr;
					//char* read_seq_id_ptr = reads[readn - 1] + 1;
					//while ((*read_seq_id_ptr != ' ') && (*read_seq_id_ptr != '\n') && (*read_seq_id_ptr != '\r')) *read_seq_arr_ptr++ = *read_seq_id_ptr++;
					//string read_seq_str = read_seq_arr;
					string read_seq_str = read.header.substr(0, read.header.find(' '));
					readstats.otu_map[ref_seq_str].push_back(read_seq_str);
				}
			}

			// output alignment to SAM or Blast-like formats
			//if (samout_gv || blastout_gv)
			//{
				// quality for FASTQ
				//char* read_qual = NULL;
				//if (read.format == Format::FASTA)
				//{
				// forward
				//if (ptr_alignment->strand)
				//	if (read.hits_align_info.alignv[p].strand)
				//	{
				// if second part of split read or last read in file
				//		if (((readn == 3) && (file_s > 0)) || (readn >= (strs - 2)))
				//		{
				//			read_qual = reads[readn];
				//			int8_t numnewlines = 0;
				//			while (numnewlines < 2) { if (*read_qual++ == '\n' || *read_qual++ == '\r') numnewlines++; }
				//		}
				//		else read_qual = reads[readn + 1] - read.hits_align_info.alignv[p].readlen - 1;
				//	}
				// reverse-complement
				//	else
				//	{
				// if second part of split read or last read in file
				//		if (((readn == 3) && (file_s > 0)) || (readn >= (strs - 2)))
				//		{
				//			read_qual = reads[readn];

				// last file section
				//			if (file_s == file_sections - 1)
				//			{
				//				while (*read_qual != '\0') read_qual++;
				//				if (*(read_qual - 3) == '\n' || *(read_qual - 3) == '\r') read_qual--;
				// account for '\n\0'
				//				read_qual -= 2;
				//			}
				// file section > 0 and < last file section
				//			else
				//			{
				//				while (read_qual != finalnt) read_qual++;
				//				read_qual--;
				//			}
				//		}
				//		else read_qual = reads[readn + 1] - 2;
				//	}
				//}//~if filesig == '@'

				if (index.opts.blastout)
				{
					uint32_t bitscore = (uint32_t)((float)((index.gumbel[index.index_num].first)
						* (read.hits_align_info.alignv[p].score1) - log(index.gumbel[index.index_num].second)) / (float)log(2));

					double evalue_score = (double)(index.gumbel[index.index_num].second) * index.full_ref[index.index_num]
						* index.full_read[index.index_num]
						* std::exp(-(index.gumbel[index.index_num].first) * read.hits_align_info.alignv[p].score1);

					output.report_blast(output.acceptedblast, index, refs, read);
					//report_blast(
					//	output.acceptedblast, //blast output file
					//	read.hits_align_info.alignv[p], //SW alignment cigar  ptr_alignment
					//	reads[readn - 1] + 1, //read name
					//	myread, //read sequence (in integer format)
					//	read_qual, //read quality
					//	reference_seq[(2 * ref_seq)] + 1, //reference name
					//	reference_seq[(2 * ref_seq) + 1], //reference sequence
					//	evalue_score, //e-value score
					//	read.hits_align_info.alignv[p].readlen, //read length (to compute the masked regions)
					//	bitscore,
					//	ptr_alignment->strand,
					//	(double)id / total_pos, // %id
					//	(double)align_len / read.hits_align_info.alignv[p].readlen, // %query coverage
					//	mismatches,
					//	gaps
					//);
				}

				if (index.opts.samout)
				{
					output.report_sam(output.acceptedblast, refs, read);
					//report_sam(
					//	acceptedsam, //sam output file
					//	ptr_alignment, //SW alignment cigar
					//	reads[readn - 1] + 1, //read name
					//	myread,//read sequence (in integer format)
					//	read_qual, //read quality
					//	reference_seq[(2 * ref_seq)] + 1, //reference name
					//	reference_seq[(2 * ref_seq) + 1], //reference sequence
					//	read.hits_align_info.alignv[p].readlen, //read length (to compute the masked regions)
					//	ptr_alignment->strand,
					//	mismatches + gaps //edit distance
					//);
				}
			//}//~if (samout_gv || blastout_gv)
		}//~if alignment at current database and index part loaded in RAM
		 //ptr_alignment++;
	}//~for all best alignments
} // ~writeAlignmentJob