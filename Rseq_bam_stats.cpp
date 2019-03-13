/*****************************************************************************
  
  Bam_stats_RNA-seq.cpp
  1) basic stats of the alignment of reads against ref.genome
  (any mapper)

  ********** this will only work for a bam file or files sorted by read name **********

  2) also calculate paired-end mapping to generate "best" alignment of a fragment
  (based on flag containing "Primary" Record), write the uniquely mapped reads into a new bam file
  
  supports both paired-end (fragment level) and single-end sequencing

  (c) 2016 - Sun Ruping
  Dept. Vingron ( Computational Mol. Bio. )
  Max-Planck-Institute for Molecular Genetics
  Ihnestr. 73, D-14195, Berlin, Germany   

  (c) 2015 - Sun Ruping
  Curtis Lab, Medicine & Geneitcs,
  Stanford University

  ruping@stanford.edu

g++ Rseq_bam_stats.cpp
-I/srv/gsfs0/projects/curtis/ruping/tools/bamtools/include/ -I/srv/gsfs0/projects/curtis/ruping/tools/zlib/current/include/ -I/srv/gsfs0/projects/curtis/ruping/tools/boost/current/include/ 
-L/srv/gsfs0/projects/curtis/ruping/tools/bamtools/lib/ -L/srv/gsfs0/projects/curtis/ruping/tools/zlib/current/lib/ -L/srv/gsfs0/projects/curtis/ruping/tools/boost/current/lib/ 
-lbamtools -lz -Wl,-rpath,/srv/gsfs0/projects/curtis/ruping/tools/bamtools/lib/:/srv/gsfs0/projects/curtis/ruping/tools/boost/current/lib/ -lboost_regex -o Rseq_bam_stats

g++ Rseq_bam_stats.cpp 
-I/home/regularhand/tools/bamtools/include/ -I/home/regularhand/tools/zlib/current/include/ -I/home/regularhand/tools/boost/current/include/ 
-L/home/regularhand/tools/bamtools/lib/ -L/home/regularhand/tools/zlib/current/lib/ -L/home/regularhand/tools/boost/current/lib/ 
-lbamtools -lz -Wl,-rpath,/home/regularhand/tools/bamtools/lib/:/home/regularhand/tools/boost/current/lib/ -lboost_regex -o Rseq_bam_stats

g++ Rseq_bam_stats.cpp
-I/N/users/ruping/ngs_tools/bamtools/current/include/ -I/N/users/ruping/ngs_tools/zlib/current/include/ -I/N/users/ruping/ngs_tools/boost/current/include/ 
-L/N/users/ruping/ngs_tools/bamtools/current/lib/ -L/N/users/ruping/ngs_tools/zlib/current/lib/ -L/N/users/ruping/ngs_tools/boost/current/lib/ 
-lbamtools -lz -Wl,-rpath,/N/users/ruping/ngs_tools/bamtools/current/lib/:/N/users/ruping/ngs_tools/boost/current/lib/ -lboost_regex -o Rseq_bam_stats
******************************************************************************/

#include <api/BamReader.h>
#include <api/BamWriter.h>
#include <api/BamMultiReader.h>

using namespace BamTools;

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <deque>
#include <set>
#include <string>
#include <cstring>
#include <sstream>
#include "Rseq_bam_stats.h"
#include <iomanip>
#include "boost/regex.hpp"
using namespace boost;
using namespace std;


struct RseqSTATS {
  unsigned int num_Reads;
  unsigned int num_Duplicates;
  unsigned int num_FailedQC;
  unsigned int num_Mapped;
  unsigned int num_Unique;
  unsigned int num_spliced;
  unsigned int num_Singletons;
  unsigned int num_ProperPair;
  unsigned int num_WrongPair;
  unsigned int num_ProperPair_dis;
  unsigned int num_WrongPair_dis;
  unsigned int num_xenoPair;                 //for human and mouse merged pair
  unsigned int num_xenoMulti;                //for reads mapping to both mouse and human
  unsigned int num_mouse;                    //for mouse reads
  unsigned int num_human;                    //for human reads
  unsigned int num_Multi;
  unsigned int num_Unmapped;
  unsigned int num_UniqueHalf;
  unsigned int num_multiMis;
};

struct Alignment {
  string chr1;
  unsigned int start1;
  unsigned int end1;
  string chr2;
  unsigned int start2;
  unsigned int end2;
  unsigned int cate; // 1:unmapped; 2:multi; 3:singleton; 4:unique;  (UN 5-9) 5:one_end_mapped; 6:1uniq; 7:2uniq; 8:1multi; 9:2multi; 10:saved_one_end_unique
  bool junction;
  BamAlignment mate1;
  BamAlignment mate2;
};


inline void ParseCigar(const vector<CigarOp> &cigar, vector<int> &blockStarts, vector<int> &blockEnds, unsigned int &alignmentEnd, bool &jc, bool &chimeric, bool &hoe, string &cliptype, unsigned int &cliplen, map<unsigned int, unsigned int> &insertions, unsigned int &softClip);
inline void splitstring(const string &str, vector<string> &elements, const string &delimiter);
inline string int2str(unsigned int &i);
inline void print_stats(struct RseqSTATS &rstats);

int main (int argc, char *argv[]) {
 
  struct parameters *param = 0;
  param = interface(param, argc, argv);

  //-------------------------------------------------------------------------------------------------------+
  // BAM input (file or filenames?)                                                                        |
  //-------------------------------------------------------------------------------------------------------+
  char *fof = param->mapping_f;
  FILE *IN=NULL;
  char line[5000];
  int filecount=0;
  vector <string> fnames;

  if (strchr(fof,' ')!=NULL) {
    char *ptr;
    ptr=strtok(fof," ");
    while (ptr!=NULL) {
      fnames.push_back(ptr);
      filecount++;
      ptr=strtok(NULL," ");
    }
  } else {
    IN=fopen(fof,"rt");
    if (IN!=NULL) {
      long linecount=0;
      while (fgets(line,5000-1,IN)!=NULL) {
        linecount++;
        if (line[0]!='#' && line[0]!='\n') {
          char *ptr=strchr(line,'\n');
          if (ptr!=NULL && ptr[0]=='\n') {
            ptr[0]='\0';
          }
          FILE *dummy=NULL;
          dummy=fopen(line,"rt");
          if (dummy!=NULL) {     // seems to be a file of filenames...
            fclose(dummy);
            fnames.push_back(line);
            filecount++;
          } else if (filecount==0 || linecount>=1000-1) {  // seems to be a single file
            fnames.push_back(fof);
            filecount++;
            break;
          }
        }
      }
      fclose(IN);
    }
  } //file or file name decided and stored in vector "fnames"

  cerr << "the input mapping files are:" << endl;
  vector <string>::iterator fit = fnames.begin();
  for(; fit != fnames.end(); fit++) {
    cerr << *fit << endl;
  }  

  //-------------------------------------------------------------------------------------------------------+
  // end of file or filenames                                                                              |
  //-------------------------------------------------------------------------------------------------------+

  unsigned int readlen = param->readlength;
  unsigned int cliplen = (int)(readlen*0.25 + 0.5);
  if (cliplen < 15) {
    cliplen = 15;
  } else if (cliplen > 21) {
    cliplen = 21;
  }
  cerr << "readlen: " << readlen << endl;
  cerr << "cliplen: " << cliplen << endl;

  unsigned int maxIntron = param->maxIntron;   // maximum intron length

  //bam input and generate index if not yet
  BamMultiReader reader;
  reader.Open(fnames);                         // the mapping bam file is opened 

  // get header & reference information
  string header = reader.GetHeaderText();
  RefVector refs = reader.GetReferenceData();

  // attempt to open BamWriter
  BamWriter writer;
  string outputBam = param->writer;
  if ( outputBam != "" ) {
    if ( !writer.Open(param->writer, header, refs) ) {
      cerr << "Could not open output BAM file" << endl;
      exit(0);
    }
  }

  // attempt to write unmapped reads
  ofstream unmapped_f;
  string unmapped = param->unmapped;
  if ( unmapped != "") {
    unmapped_f.open(param->unmapped);
  }

  // attempt to write arp reads
  ofstream arp_f;
  string arp = param->arp;
  if ( arp != "" ) {
    arp_f.open(param->arp);
  }

  // attemp to write breakpoints
  ofstream bp_f;
  string bp_file = param->breakpoint;
  if ( bp_file != "" ) {
    bp_f.open(param->breakpoint);
  }

  // statistics
  struct RseqSTATS BAMSTATS = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

  map <string, struct Alignment> fragment; // map for fragment

  // type == "s" or type == "p" ?
  string type = param->type;
  
  string old_frag = "SRP";

  BamAlignment bam;
  while ( reader.GetNextAlignment(bam) ) {

    BamAlignment cBAM;

    unsigned int unique = 0;
    string XS = "SRP";
    bool jc = false;
    bool chimeric = false;
    string cliptype = "N";
    bool hoe = true;                               //true is head
    string mateStatus = "p";                       //'w' hints breakpoints
    unsigned int breakpoint = 0;
    string chrom = "SRP";
    string strand = "+";
    unsigned int alignmentStart = 0;
    unsigned int alignmentEnd = 0;
    unsigned int cigarEnd;
    vector <int> blockLengths;
    vector <int> blockStarts;
    string mateChr = "SRP";
    unsigned int matePos = 0;
    map<unsigned int, unsigned int> insertions;       // for insertions                                        
    unsigned int softClip = 0;                        // for soft clipping            
    bool whetherWrite = false;    
    if ( outputBam != "" ) {
      whetherWrite = true;
    }
    if (readlen == 0) {
      readlen = bam.Length;
    }

    //cerr << bam.Name << "endl";
    
    ++BAMSTATS.num_Reads;

    if ( bam.IsMapped() == true || bam.IsMateMapped() == true ) {  // either this mate or the other mate is mapped

      ++BAMSTATS.num_Mapped;
      if (bam.IsDuplicate() == true) {
        ++BAMSTATS.num_Duplicates;
        continue;                                  // skip duplicated reads
      }

      if ( bam.HasTag("NH") ) {
        bam.GetTag("NH", unique);                   // rnaseq aligners, such as gsnap and tophat
      } else {
        if (bam.MapQuality > 10) {                  // other aligner
          unique = 1;
        }
      }


      blockStarts.push_back(0);
      ParseCigar(bam.CigarData, blockStarts, blockLengths, cigarEnd, jc, chimeric, hoe, cliptype, cliplen, insertions, softClip); 

      chrom  = refs.at(bam.RefID).RefName;          // chromosome
      if (bam.IsReverseStrand()) strand = "-";      // strand -
      alignmentStart = bam.Position+1;              // start
      alignmentEnd   = bam.GetEndPosition();        // end

      //        for xenograft counting             //
      //unsigned int num_xenoPair;                 //for human and mouse merged pair
      //unsigned int num_xenoMulti;                //for reads mapping to both mouse and human
      //unsigned int num_mouse;                    //for mouse reads
      //unsigned int num_human;                    //for human reads
      bool isMouse = false;
      if ( chrom.substr(0,3) == "chr" ) {
        isMouse = true;
      }

      mateChr = refs.at(bam.MateRefID).RefName;
      bool isMateMouse = false;
      if ( mateChr.substr(0,3) == "chr" ) {
        isMateMouse = true;
      }

      if ( isMouse == true || isMateMouse == true) {
        whetherWrite = false;
        ++BAMSTATS.num_mouse;
      } else {
        ++BAMSTATS.num_human;
      }

      string XA;
      bool isMultiMouse = false;
      bool isMultiHuman = false;
      if ( bam.HasTag("XA") ) {
        bam.GetTag("XA", XA);                   // get alternative mapping location
        vector <string> otherMappings;
        splitstring(XA, otherMappings, ";");
        vector<string>::iterator omiter = otherMappings.begin();
        vector<string>::iterator omiterEnd = otherMappings.end();
        for (; omiter != omiterEnd; ++omiter) {
          if ((*omiter).substr(0,3) == "chr") {
            isMultiMouse = true;
          } else {
            isMultiHuman = true;
          }
        }
        if ((isMouse == true && isMultiHuman == true) || (isMouse == false && isMultiMouse == true)) {
          ++BAMSTATS.num_xenoMulti;
          whetherWrite = false;
        }
        //cerr << bam.Name << "\t" << XA << "\t" << isMouse << "\t" << isMultiMouse << endl;
      }  //  with XA tag

      if (bam.IsMapped() == true && bam.IsMateMapped() == true) {   // both ends mapped
          mateChr = refs.at(bam.MateRefID).RefName;
          matePos = bam.MatePosition;
          int mateDistance = matePos-alignmentStart;
          if (mateChr == chrom && abs(mateDistance) < maxIntron) {
            if (bam.IsProperPair() == false) {
              bam.SetIsProperPair(true);
              //cout << bam.IsProperPair() << "\t" << bam.AlignmentFlag << "\n";
            }
          }
   
          if ( isMouse != isMateMouse ) {     // xenopair
            ++BAMSTATS.num_xenoPair;
          }
      }   //both ends mapped

      if (whetherWrite == true) {
        writer.SaveAlignment(bam);                    // write mapped tags to a new bam
      }

      if ( bam.IsMapped() == false ) {                // skip the one with only mate mappable
        continue;
      }

      if ( unique == 1 ) {                            // check breakpoint reads

        ++BAMSTATS.num_Unique;

        //processing MD string, calculate num of mismatches 
        string MD;
        bam.GetTag("MD", MD);

        unsigned int num_mismatches = 0; 
        num_mismatches += insertions.size();
        map<unsigned int, unsigned int>::iterator inserit_index = insertions.begin();

        unsigned int cuPos = alignmentStart;
        unsigned int cuPosRead = softClip + 1;

        regex rgx( "([0-9]+)([ACGTacgt]|\\^[ACGTacgt]+)" );
        int subs[] = {1,2};
        sregex_token_iterator rit ( MD.begin(), MD.end(), rgx, subs );
        sregex_token_iterator rend;

        //cerr << bam.Name << "\t";

        while ( rit != rend ) {

          unsigned int incre = atoi((*rit).str().c_str());                  //number 1                                               
          cuPos += incre;                                                   //number 1
          cuPosRead += incre;

          if (blockStarts.size() > 1) {   //judge which block the mutation locate
            vector <int>::iterator bliter2 = blockLengths.begin();
            vector <int>::iterator bSiter2 = blockStarts.begin();
            unsigned int culength = 0;
            while (bliter2 != blockLengths.end() && bSiter2 != blockStarts.end()) {
              if (cuPosRead <= (culength + *bliter2)) {
                cuPos += (*bSiter2 - culength);
                break;
              }
              culength += *bliter2;
              bliter2++;
              bSiter2++;
            }
          } //multi blocks especially useful for RNA-seq junction reads 

          map<unsigned int, unsigned int>::iterator inserit = inserit_index;
          while ( inserit != insertions.end() ) {
            if ( inserit->first < cuPosRead ) {
              cuPosRead += inserit->second;
              inserit++;
              inserit_index = inserit;
            } else {
              inserit_index = inserit;
              break;
            }
          }

          ++rit;                                            //round 1 addition

          if (((*rit).str())[0] == '^') {                   //variant 2 
            incre = (*rit).length() - 1;                    //variant 2
            cuPos += incre;                                 //variant 2
            ++num_mismatches;

          } else if ((*rit).length() == 1) {                // single base nucleotide change
            //check whether it is "N" or not 
            //cerr << cuPosRead << "\t";
            string baseInReadPre = (bam.QueryBases).substr( cuPosRead-1, 1 );
            if (baseInReadPre != "N") {
              ++num_mismatches;
            }
            cuPos += 1;
            cuPosRead += 1;
          } else {
            cerr << "wired thing happened in the MD string of " << bam.Name << endl;
            exit(1);
          }

          ++rit;                                           
        } //loop for all MD characters 
        //cerr << endl;

        if (num_mismatches >= 2) {      //multi mismatches
          ++BAMSTATS.num_multiMis;
        }

        if (chimeric == true) {

          if (type == "p") {
            if ( bam.IsMateMapped() == true){
              mateChr = refs.at(bam.MateRefID).RefName;
              matePos = bam.MatePosition;
              int mateDistance = matePos-alignmentStart;
              if (mateChr != chrom || abs(mateDistance) > maxIntron) 
                mateStatus = "w";
            }
          }

          if (hoe  == false) {
            breakpoint =  alignmentStart;
          }
          else if (hoe == true) {
            vector<int>::iterator bsiter = blockStarts.end();
            vector<int>::iterator bliter = blockLengths.end();
            breakpoint = alignmentStart + *(bsiter-1) + *(bliter-1);
          }
          if ( bp_file != "" ) {
            bp_f << chrom << "\t" << breakpoint << "\t" << bam.Name << "\t" << mateStatus << "\t" << cliptype << endl;
          }
        }

      } // check breakpoint reads

    } else {
      ++BAMSTATS.num_Unmapped;
      if ( unmapped != "" ) {
        unmapped_f << bam.Name << endl;
      }
    }

    if (type == "fixflag" || type == "xeno" || type == "multiMis") {   //just to fix flag
       continue;
    }

    if (type == "s") {  //single-end
      if ( bam.Name != old_frag ) {  // new frag

        ++BAMSTATS.num_Reads;
        if ( bam.IsMapped() ) { //mapped
          ++BAMSTATS.num_Mapped;
          if ( unique == 1 ) ++BAMSTATS.num_Unique;
          else               ++BAMSTATS.num_Multi;
        } else { //unmapped
           ++BAMSTATS.num_Unmapped;
        }
        if ( bam.IsDuplicate() ) ++BAMSTATS.num_Duplicates;
        if (  bam.IsFailedQC() ) ++BAMSTATS.num_FailedQC;
        if (jc == true)          ++BAMSTATS.num_spliced;

        old_frag = bam.Name; // reset old frag
      } else {  // IT IS AN OLD FRAGMENT!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
      }
    } else { // paired-end
      unsigned int mate = 1;
      if ( bam.IsFirstMate() == false ) mate = 2;           // second mate

      if ( bam.Name != old_frag ) {  // new frag

        if (old_frag != "SRP") {
          fragment.erase(old_frag);    // remove old_frag
        }

        if ( bam.IsMapped() == false && bam.IsMateMapped() == false ) {  // unmapped
          ++BAMSTATS.num_Reads;
          ++BAMSTATS.num_Unmapped;
          struct Alignment tmp = {"UM", 0, 0, "UM", 0, 0, 1, jc, cBAM, cBAM};
          fragment.insert( pair<string, struct Alignment>(bam.Name, tmp) );
        } // unmapped
        else if ( bam.IsMapped() == false && bam.IsMateMapped() == true ) {  // one end is not mappable
          if (mate == 1){
            struct Alignment tmp = {"UM", 0, 0, "SRP", 0, 0, 5, jc, bam, cBAM};
            fragment.insert( pair<string, struct Alignment>(bam.Name, tmp) );
          }
          else {
            struct Alignment tmp = {"SRP", 0, 0, "UM", 0, 0, 5, jc, cBAM, bam};
            fragment.insert( pair<string, struct Alignment>(bam.Name, tmp) );
          }
        } // undecided one end not mappable
        else if ( bam.IsMapped() == true && bam.IsMateMapped() == false ) {  // one other end is not mappable

          if (unique > 1) {  // one end multiple mapped, the other end not mappable
            ++BAMSTATS.num_Reads;
            ++BAMSTATS.num_Mapped;
            ++BAMSTATS.num_Multi;
            if (mate == 1) {
              struct Alignment tmp = {"MM", 0, 0, "UM", 0, 0, 2, jc, cBAM, cBAM};
              fragment.insert( pair<string, struct Alignment>(bam.Name, tmp) );
            }
            else {
              struct Alignment tmp = {"UM", 0, 0, "MM", 0, 0, 2, jc, cBAM, cBAM};
              fragment.insert( pair<string, struct Alignment>(bam.Name, tmp) );
            }
          } // one end multiple mapped, the other end not mappable
          else { // Singletons (it should be output here, since TopHat does not output the alignment of other mate)
            ++BAMSTATS.num_Reads;
            ++BAMSTATS.num_Mapped;
            ++BAMSTATS.num_Unique;
            ++BAMSTATS.num_Singletons;
            ++BAMSTATS.num_WrongPair;
            ++BAMSTATS.num_WrongPair_dis;
            if (jc == true) ++BAMSTATS.num_spliced;
            if (mate == 1) {
              struct Alignment tmp = {chrom, alignmentStart, alignmentEnd, "UM", 0, 0, 3, jc, bam, cBAM};
              fragment.insert( pair<string, struct Alignment>(bam.Name, tmp) );
            }
            else {
              struct Alignment tmp = {"UM", 0, 0, chrom, alignmentStart, alignmentEnd, 3, jc, cBAM, bam};
              fragment.insert( pair<string, struct Alignment>(bam.Name, tmp) );
            }
            if ( arp != "" ) arp_f << bam.Name << endl;       // write arp
          } // Singletons

        } //one another end is not mappable

        else {  // both ends mapped
          if ( unique == 1 ) {  // current end is uniquelly mapped
            if (mate == 1) {
              struct Alignment tmp = {chrom, alignmentStart, alignmentEnd, refs.at(bam.MateRefID).RefName, (bam.MatePosition+1), 0, 6, jc, bam, cBAM};
              fragment.insert( pair<string, struct Alignment>(bam.Name, tmp) );
            }
            else {
              struct Alignment tmp = {refs.at(bam.MateRefID).RefName, (bam.MatePosition+1), 0, chrom, alignmentStart, alignmentEnd, 7, jc, cBAM, bam};
              fragment.insert( pair<string, struct Alignment>(bam.Name, tmp) );
            }
          } // current unique
          else {  // current end is not unique
            if (mate == 1) {
              if ( bam.IsPrimaryAlignment() == true ){
                struct Alignment tmp = {chrom, alignmentStart, alignmentEnd, refs.at(bam.MateRefID).RefName, (bam.MatePosition+1), 0, 8, jc, bam, cBAM};
                fragment.insert( pair<string, struct Alignment>(bam.Name, tmp) );
              }
              else {
                struct Alignment tmp = {"SRP", 0, 0, "SRP", 0, 0, 8, jc, cBAM, cBAM};
                fragment.insert( pair<string, struct Alignment>(bam.Name, tmp) );
              }
            }
            else { // mate 2
              if ( bam.IsPrimaryAlignment() == true ){
                struct Alignment tmp = {refs.at(bam.MateRefID).RefName, (bam.MatePosition+1), 0, chrom, alignmentStart, alignmentEnd, 9, jc, cBAM, bam};
                fragment.insert( pair<string, struct Alignment>(bam.Name, tmp) );
              }
              else {
                struct Alignment tmp = {"SRP", 0, 0, "SRP", 0, 0, 9, jc, cBAM, cBAM};
                fragment.insert( pair<string, struct Alignment>(bam.Name, tmp) );
              }
            }
          } // current multi
        } // both ends mapped;

        old_frag = bam.Name; // reset old frag

      } // a new frag;

      else {  // IT IS AN OLD FRAGMENT!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

        if (fragment[bam.Name].cate == 5) { // one end mapped the other end not, but the mapped end is not decided;
          if ( unique > 1 ) {  // one end multiple mapped, the other end not mappable
            ++BAMSTATS.num_Reads;
            ++BAMSTATS.num_Mapped;
            ++BAMSTATS.num_Multi;
            if (mate == 1){
              fragment[bam.Name].chr1 = "MM";          
            }
            else {
              fragment[bam.Name].chr2 = "MM";
            }
            fragment[bam.Name].cate = 2;
          } // one end multiple mapped, the other end not mappable
          else { // Singletons
            ++BAMSTATS.num_Reads;
            ++BAMSTATS.num_Mapped;
            ++BAMSTATS.num_Unique;
            ++BAMSTATS.num_Singletons;
            ++BAMSTATS.num_WrongPair;
            ++BAMSTATS.num_WrongPair_dis;
            if (jc == true) ++BAMSTATS.num_spliced;
            if (mate == 1){
              fragment[bam.Name].chr1 = chrom;
              fragment[bam.Name].start1 = alignmentStart;
              fragment[bam.Name].end1 = alignmentEnd;
              fragment[bam.Name].junction = jc;
              fragment[bam.Name].mate1 = bam;
              if ( arp != "" ) arp_f << bam.Name << endl;       // write arp
            }
            else {
              fragment[bam.Name].chr2 = chrom;
              fragment[bam.Name].start2 = alignmentStart;
              fragment[bam.Name].end2 = alignmentEnd;
              fragment[bam.Name].junction = jc;
              fragment[bam.Name].mate2 = bam;
              if ( arp != "" ) arp_f << bam.Name << endl;       // write arp
            }
            fragment[bam.Name].cate = 3;
          } // Singletons
        } //cate == 5

        else if (fragment[bam.Name].cate == 6) { // mate 1 is unique
          if (mate == 1) {
            if (bam.CigarData.size() == 1){
              cerr << "mate1 unique inconsistency, exit\n"; cerr << "problem reads: " << bam.Name << endl; cerr << "cigar: " << bam.CigarData.size() << endl; exit(0);
            }
            else { //print out the current alignment
              //writer.SaveAlignment(bam);                          // write
              if ( arp != "" ) {
                arp_f << bam.Name << endl;
              }
              continue;
            }
          } 
          if (unique == 1) { // both ends are unique VERY GOOD
            ++BAMSTATS.num_Reads;
            ++BAMSTATS.num_Mapped;
            ++BAMSTATS.num_Unique;
            if ( bam.IsDuplicate() ) ++BAMSTATS.num_Duplicates;
            if (  bam.IsFailedQC() ) ++BAMSTATS.num_FailedQC;
            if ( bam.IsProperPair()) ++BAMSTATS.num_ProperPair;
            else                     ++BAMSTATS.num_WrongPair;
            if (jc == true || fragment[bam.Name].junction == true) ++BAMSTATS.num_spliced;
            fragment[bam.Name].end2 = alignmentEnd;
            fragment[bam.Name].cate = 4;  //unique
            fragment[bam.Name].mate2 = bam;

            int dis = fragment[bam.Name].start1 - fragment[bam.Name].start2;
            if ( (fragment[bam.Name].chr1 != fragment[bam.Name].chr2) || (abs(dis) > maxIntron) ) {
              ++BAMSTATS.num_WrongPair_dis;
              if ( arp != "" ) {
                arp_f << bam.Name << endl;
              }
            } else {
              ++BAMSTATS.num_ProperPair_dis;
            }

          } // both ends are unique
          else { // the mate 2 is multi, try to figure out the "primary" record
            if ( bam.IsPrimaryAlignment() == true ) { // if this is a primary result
              ++BAMSTATS.num_Reads;
              ++BAMSTATS.num_Mapped;
              ++BAMSTATS.num_UniqueHalf;
              fragment[bam.Name].chr2   = chrom;
              fragment[bam.Name].start2 = alignmentStart;
              fragment[bam.Name].end2   = alignmentEnd;
              fragment[bam.Name].cate   = 10;
              fragment[bam.Name].mate2 = bam;

              int dis = fragment[bam.Name].start1 - fragment[bam.Name].start2;
              if ( (fragment[bam.Name].chr1 != fragment[bam.Name].chr2) || (abs(dis) > maxIntron) ) {
                if ( arp != "" ) {
                  arp_f << bam.Name << endl;
                }
              }
            }
          }
        } // cate == 6         
        else if (fragment[bam.Name].cate == 7) { // mate 2 is unique
          if (mate == 2) {
            if (bam.CigarData.size() == 1){
              cerr << "mate2 unique inconsistency, exit\n"; cerr << "problem reads: " << bam.Name << endl; cerr << "cigar: " << bam.CigarData.size() << endl; exit(0);
            }
            else { //print out the current alignment
              //writer.SaveAlignment(bam);                          // write
              if ( arp != "" ) {
                arp_f << bam.Name << endl;
              }
              continue;
            }
          }
          if (unique == 1) { // both ends are unique VERY GOOD
            ++BAMSTATS.num_Reads;
            ++BAMSTATS.num_Mapped;
            ++BAMSTATS.num_Unique;
            if ( bam.IsDuplicate() ) ++BAMSTATS.num_Duplicates;
            if (  bam.IsFailedQC() ) ++BAMSTATS.num_FailedQC;
            if ( bam.IsProperPair()) ++BAMSTATS.num_ProperPair;
            else                     ++BAMSTATS.num_WrongPair;
            if (jc == true || fragment[bam.Name].junction == true) ++BAMSTATS.num_spliced;
            fragment[bam.Name].end1 = alignmentEnd;
            fragment[bam.Name].cate = 4;  //unique
            fragment[bam.Name].mate1 = bam;

            int dis = fragment[bam.Name].start1 - fragment[bam.Name].start2;
            if ( (fragment[bam.Name].chr1 != fragment[bam.Name].chr2) || (abs(dis) > maxIntron) ){ 
              if ( arp != "" ) {
                arp_f << bam.Name << endl;
              }
              ++BAMSTATS.num_WrongPair_dis;
            } else {
              ++BAMSTATS.num_ProperPair_dis;
            }
          } // both ends are unique
          else { // the mate 1 is multi
            if ( bam.IsPrimaryAlignment() == true ) { // if this is a primary result
              ++BAMSTATS.num_Reads;
              ++BAMSTATS.num_Mapped;
              ++BAMSTATS.num_UniqueHalf;
              fragment[bam.Name].chr1   = chrom;
              fragment[bam.Name].start1 = alignmentStart;
              fragment[bam.Name].end1   = alignmentEnd;
              fragment[bam.Name].cate   = 10;
              fragment[bam.Name].mate1 = bam;
              if ( arp != "" ) {
                int dis = fragment[bam.Name].start1 - fragment[bam.Name].start2;
                if ( (fragment[bam.Name].chr1 != fragment[bam.Name].chr2) || (abs(dis) > maxIntron) ) 
                  arp_f << bam.Name << endl;
              }
            }
          }
        } // cate == 7
        else if (fragment[bam.Name].cate == 8) {   // mate 1 is multi
          if ( fragment[bam.Name].chr1 == "SRP" ){  // meaning it is multi, but the primary is not decided
            if ( mate == 1 ){
              if ( bam.IsPrimaryAlignment() == true ){
                fragment[bam.Name].chr1   = chrom;
                fragment[bam.Name].start1 = alignmentStart;
                fragment[bam.Name].end1   = alignmentEnd;
                fragment[bam.Name].mate1  = bam;
              }
            } 
          }  // the primary is not decided

          if ( mate == 2 ) {     // check mate 2
            if ( unique == 1 ) { // if mate 2 is unique
              ++BAMSTATS.num_Reads;
              ++BAMSTATS.num_Mapped;
              ++BAMSTATS.num_UniqueHalf;
              fragment[bam.Name].chr2   = chrom;
              fragment[bam.Name].start2 = alignmentStart;
              fragment[bam.Name].end2   = alignmentEnd;
              fragment[bam.Name].cate   = 10;
              fragment[bam.Name].mate2  = bam;
              if ( arp != "" ) {
                int dis = fragment[bam.Name].start1 - fragment[bam.Name].start2;
                if ( (fragment[bam.Name].chr1 != fragment[bam.Name].chr2) || (abs(dis) > maxIntron) ) 
                  arp_f << bam.Name << endl;
              }
            }
            else { // if mate 2 is multi
              ++BAMSTATS.num_Reads;
              ++BAMSTATS.num_Mapped;
              ++BAMSTATS.num_Multi;
              fragment[bam.Name].cate = 2;
            }
          } // check mate 2  
        } // cate == 8
        else if (fragment[bam.Name].cate == 9) { // mate 2 is multi
          if ( fragment[bam.Name].chr2 == "SRP" ) {  // meaning it is multi, but the primary is not decided
            if ( mate == 2 ){
              if ( bam.IsPrimaryAlignment() == true ){
                fragment[bam.Name].chr2   = chrom;
                fragment[bam.Name].start2 = alignmentStart;
                fragment[bam.Name].end2   = alignmentEnd;
                fragment[bam.Name].mate2  = bam;
              }
            } 
          }  // the primary is not decided

          if ( mate == 1 ) {     // check mate 1
            if ( unique == 1 ) { // if mate 1 is unique
              ++BAMSTATS.num_Reads;
              ++BAMSTATS.num_Mapped;
              ++BAMSTATS.num_UniqueHalf;
              fragment[bam.Name].chr1   = chrom;
              fragment[bam.Name].start1 = alignmentStart;
              fragment[bam.Name].end1   = alignmentEnd;
              fragment[bam.Name].cate   = 10;
              fragment[bam.Name].mate1  = bam;
              if ( arp != "" ) {
                int dis = fragment[bam.Name].start1 - fragment[bam.Name].start2;
                if ( (fragment[bam.Name].chr1 != fragment[bam.Name].chr2) || (abs(dis) > maxIntron) ) 
                  arp_f << bam.Name << endl;
              }
            }
            else { // if mate 1 is multi
              ++BAMSTATS.num_Reads;
              ++BAMSTATS.num_Mapped;
              ++BAMSTATS.num_Multi;
              fragment[bam.Name].cate = 2;
            }
          } // check mate 1 

        } // cate == 9

      } // old fragment
    } // paired-end

  }  //  read a bam
      
  reader.Close();
  writer.Close();
  arp_f.close();

  print_stats(BAMSTATS);

  return 0;
} //main

inline string int2str(unsigned int &i){
  string s;
  stringstream ss(s);
  ss << i;
  return ss.str();
}

inline void splitstring(const string &str, vector<string> &elements, const string &delimiter) {
  string::size_type lastPos = str.find_first_not_of(delimiter, 0);
  string::size_type pos     = str.find_first_of(delimiter, lastPos);

  while (string::npos != pos || string::npos != lastPos) {
    elements.push_back(str.substr(lastPos, pos - lastPos));
    lastPos = str.find_first_not_of(delimiter, pos);
    pos = str.find_first_of(delimiter, lastPos);
  }
}


inline void ParseCigar(const vector<CigarOp> &cigar, vector<int> &blockStarts, vector<int> &blockLengths, unsigned int &alignmentEnd, bool &jc, bool &chimeric, bool &hoe, string &cliptype, unsigned int &cliplen, map<unsigned int, unsigned int> &insertions, unsigned int &softClip) {

  int currPosition = 0;
  int blockLength  = 0;

  unsigned int insertSize;
  unsigned int insertPos;


  //  Rip through the CIGAR ops and figure out if there is more
  //  than one block for this alignment
  vector<CigarOp>::const_iterator cigItr = cigar.begin();
  vector<CigarOp>::const_iterator cigEnd = cigar.end();
  for (; cigItr != cigEnd; ++cigItr) {
    switch (cigItr->Type) {
    case ('M') :
      blockLength  += cigItr->Length;
      currPosition += cigItr->Length;
      break;
    case ('I') : 
      insertSize = cigItr->Length;
      insertPos = currPosition + 1;
      insertions.insert( pair <unsigned int, unsigned int> (insertPos, insertSize) );
      break;
    case ('S') :
      if (currPosition == 0) { //only take action for the beginning clipping                                  
        softClip = cigItr->Length;
      }
      if (cigItr->Length >= cliplen) {
        if ( cigItr == cigar.begin() ) {              // first bases are skiped
          chimeric = true;                            // here for chimeric, but should check for the length of 'S'
          cliptype = "S";
          hoe = false;
        }
        else if ( cigItr == (cigEnd - 1) ) {
          chimeric = true;
          cliptype = "S";
        }
      }
      break;
    case ('D') :
      blockLength  += cigItr->Length;
      currPosition += cigItr->Length;
      break;
    case ('P') : break;
    case ('N') :
      blockStarts.push_back(currPosition + cigItr->Length);
      blockLengths.push_back(blockLength);
      currPosition += cigItr->Length;
      blockLength = 0;
      jc = true;
      break;
    case ('H') :                                     // here for chimeric
      if ( cigItr ==  cigar.begin() ) {              // first bases are skiped
          chimeric = true;                           // here for chimeric, but should check for the length of 'S'
          cliptype = "H";
          hoe = false;
      } else if ( cigItr == (cigEnd - 1) ) {
          chimeric = true;
          cliptype = "H";
      }
      break;
    default    :
      printf("ERROR: Invalid Cigar op type\n");   // shouldn't get here
      exit(1);
    }
  }
  // add the kast block and set the
  // alignment end (i.e., relative to the start)
  blockLengths.push_back(blockLength);
  alignmentEnd = currPosition;
}


inline void print_stats(struct RseqSTATS &rstats) {
  cout << "Reads:         " << rstats.num_Reads      << endl;
  cout << "Mapped:        " << rstats.num_Mapped     << endl;
  cout << "Unmapped:      " << rstats.num_Unmapped   << endl;
  cout << "Unique:        " << rstats.num_Unique     << endl;
  cout << "Uniquehalf:    " << rstats.num_UniqueHalf << endl;
  cout << "duplicates:    " << rstats.num_Duplicates << endl;
  cout << "multiMis:      " << rstats.num_multiMis   << endl;
  cout << "failed_QC:     " << rstats.num_FailedQC   << endl;
  cout << "singletons:    " << rstats.num_Singletons << endl;
  cout << "ProperPair:    " << rstats.num_ProperPair << endl;
  cout << "ProperPairDis: " << rstats.num_ProperPair_dis << endl;
  cout << "WrongPair:     " << rstats.num_WrongPair  << endl;
  cout << "WrongPairDis:  " << rstats.num_WrongPair_dis  << endl;
  cout << "Spliced:       " << rstats.num_spliced    << endl;
  cout << "MultiMap:      " << rstats.num_Multi      << endl;
  cout << "xenoPair:      " << rstats.num_xenoPair   << endl;
  cout << "xenoMulti:     " << rstats.num_xenoMulti  << endl;
  cout << "mouseReads:    " << rstats.num_mouse      << endl;
  cout << "humanReads:    " << rstats.num_human      << endl;
}
