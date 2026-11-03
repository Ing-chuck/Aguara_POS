# This script is meant to automatically increase the version number
# defined in src/interfaces/version.h
# uses the GitPython module to check if there are changes in the repository (not counting changes to version.h itself)
# if there are, make sure to increase the patch version
# might be expanded to create tags based on the version number

# Should be added as a build step in Qt creator
# before the make command (any working directory: paths are relative to this script)

import os
from git import Repo

repoDir = os.path.dirname(os.path.abspath(__file__))
versionPath = os.path.join(repoDir, 'src', 'interfaces', 'version.h')

def getFileName(path):
    return path.split('/')[-1]

#attempt to open the repository
repo = Repo(repoDir)

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
                words = line.split()
                # only the "#define VERSION_PATCH <n>" line
                if(len(words) == 3 and words[0] == '#define' and words[1] == 'VERSION_PATCH'):
                    try:
                        words[-1] = str(int(words[-1]) + 1)
                        line = ' '.join(words) + '\n'
                    except ValueError:
                        pass    # leave the line as it is
                lines.append(line)

        #write the updated file
        with open(versionPath, mode='w') as vFile:
            for line in lines:
                vFile.write(line)
