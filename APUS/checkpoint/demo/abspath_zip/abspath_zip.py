#!/usr/bin/env python

"""
Author: Jingle
Date:	April 28,2016
Description:
Test whether a abslute path can put into zip file and can be extracted when unzip.
"""
import os
import zipfile
import shutil

ABS_DIR="/data"
CURR_ZIP_DIR="current"

ZIP_FILE_NAME="test.zip"

#zip current dir

zipBaseName = "ck"
zipAbsName = shutil.make_archive(zipBaseName,'zip',CURR_ZIP_DIR)
print "got base zip file: %s"%(zipAbsName)

zf = zipfile.ZipFile(zipAbsName, "a")
for root, dirs, files in os.walk(ABS_DIR):
	#print "root: %s, dirs:%s"%(root,dirs)
	#print files
	#print "================"
	zf.write(root,root)
	for f in files:
		p = os.path.join(root,f)
		print "path: %s"%(p)
		zf.write(p,"../"+p)


	
	
	
