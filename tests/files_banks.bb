; files, streams, directories and banks
f = WriteFile("zenblitz_test.dat")
WriteLine f, "first line"
WriteLine f, "second line"
WriteInt f, 123456
WriteShort f, 65535
WriteByte f, 200
WriteFloat f, 1.5
WriteString f, "packed string"
CloseFile f
Print "size=" + FileSize("zenblitz_test.dat")
Print "type=" + FileType("zenblitz_test.dat") + " dir=" + FileType(".") + " none=" + FileType("does_not_exist_xyz")
f = ReadFile("zenblitz_test.dat")
Print ReadLine(f)
Print ReadLine(f)
Print ReadInt(f)
Print ReadShort(f)
Print ReadByte(f)
Print ReadFloat(f)
Print ReadString(f)
Print "eof=" + Eof(f)
CloseFile f
f = OpenFile("zenblitz_test.dat")
SeekFile f, 6
Print "pos=" + FilePos(f) + " " + ReadLine(f)
CloseFile f
CopyFile "zenblitz_test.dat", "zenblitz_copy.dat"
Print "copy=" + FileSize("zenblitz_copy.dat")
DeleteFile "zenblitz_copy.dat"
Print "deleted=" + FileType("zenblitz_copy.dat")

; read all lines
n = 0
f = ReadFile("zenblitz_test.dat")
While Not Eof(f)
	l$ = ReadLine(f)
	n = n + 1
	If n > 10 Then Exit
Wend
CloseFile f
Print "lines read: " + n

; directories
CreateDir "zenblitz_dir"
f = WriteFile("zenblitz_dir/a.txt"): WriteLine f, "a": CloseFile f
d = ReadDir("zenblitz_dir")
found = 0
Repeat
	e$ = NextFile(d)
	If e = "" Then Exit
	If e = "a.txt" Then found = 1
Forever
CloseDir d
Print "found a.txt: " + found
DeleteFile "zenblitz_dir/a.txt"
DeleteDir "zenblitz_dir"
Print "dir gone: " + (FileType("zenblitz_dir") = 0)

; banks
b = CreateBank(16)
Print "bank size " + BankSize(b)
PokeByte b, 0, 255
PokeShort b, 2, 4660
PokeInt b, 4, -2
PokeFloat b, 8, 2.5
Print PeekByte(b, 0) + " " + PeekShort(b, 2) + " " + PeekInt(b, 4) + " " + PeekFloat(b, 8)
ResizeBank b, 32
Print "resized " + BankSize(b) + " keeps " + PeekInt(b, 4)
b2 = CreateBank(8)
CopyBank b, 4, b2, 0, 4
Print "copied " + PeekInt(b2, 0)
f = WriteFile("zenblitz_bank.dat")
Print "wrote " + WriteBytes(b, f, 0, 12)
CloseFile f
b3 = CreateBank(12)
f = ReadFile("zenblitz_bank.dat")
Print "read " + ReadBytes(b3, f, 0, 12)
CloseFile f
Print PeekByte(b3, 0) + " " + PeekInt(b3, 4) + " " + PeekFloat(b3, 8)
FreeBank b
FreeBank b2
FreeBank b3
DeleteFile "zenblitz_bank.dat"
DeleteFile "zenblitz_test.dat"
Print "done"
