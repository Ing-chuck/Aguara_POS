# This script is meant to automatically increase the version number
# defined in version.h of the core program
# usesGitPython module to check if there are changes in the repository (not counting changes to version.h itself)
# if there are, make sure to increse the patch version
# might be expanded to create tags based on the version number

# Should be added as a build step in Qt creator
# before the make command

import os
from git import Repo

versionPath = './src/core/version.h'

def getFileName(path):
    return path.split('/')[-1]

#attempt to open the repository
wd = os.getcwd()
repo = Repo(wd)

#only update if there are changes
if(repo.is_dirty()):
    
    #load diffs agains working coppy
    hcDiff = repo.head.commit.diff(None)

    #see if version.h has been modified
    if('version.h' not in [getFileName(item.a_path) for item in hcDiff.iter_change_type('M')]):
        #if not, we update the version number
        lines = []
        with open(versionPath, mode='r') as vFile:
            for line in vFile:
                if('VERSION_PATCH' in line):
                    words = line.split()
                    try:
                        words[-1] = str(int(words[-1]) + 1)
                    except ValueError:
                        #ignore this error
                        l = 0
                    line = ' '.join(words) + '\n'
                lines.append(line)

        #write the updated file
        with open(versionPath, mode='w') as vFile:
            for line in lines:
                vFile.write(line)
        

 
