#!/usr/bin/python

import sys
import os

def read_contig(infile):
	contig = infile.readline()

	if not contig:
		return None, None

	delim = contig[0]

	line = infile.readline()
	c_len = 0
	while line and line.find(delim) != 0:
		contig += line
		c_len += len(line) - 1
		line = infile.readline()

	if not line:
		return c_len, contig

	infile.seek(infile.tell() - len(line))
	
	return c_len, contig
	


if len(sys.argv) < 3:
	print("Usage: " + sys.argv[0] + " <source> <threshold>")	
	sys.exit()

inFileName = sys.argv[1]
inFile = open(inFileName, "r")
threshold = int(sys.argv[2])

fName, ext = os.path.splitext(inFileName)
fileS = open(fName + "_short_"+str(threshold) + ext, "w") 
fileL = open(fName + "_long_"+str(threshold) + ext, "w")

c_len, contig = read_contig(inFile)
while contig is not None:
	if c_len <= threshold:
		fileS.write(contig)
	else:
		fileL.write(contig)
		
	c_len, contig = read_contig(inFile)


inFile.close()
fileS.close()
fileL.close()

