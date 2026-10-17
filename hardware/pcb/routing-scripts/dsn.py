import sys, pcbnew
b = pcbnew.LoadBoard(sys.argv[1])
print("export", pcbnew.ExportSpecctraDSN(b, sys.argv[2]))
