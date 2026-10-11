from c64.disk import open, close, write_text


filename = "pyc64-case,s,w"
text = "Abc\nx"
opened = open(2, 8, 2, filename)
written = write_text(2, text)
close(2)
unchanged = filename == "pyc64-case,s,w" and text == "Abc\nx"
letter = ord(text[1])
newline = ord(text[3])
print("TEXT IO:", opened, written, unchanged)
print("ASCII:", letter, newline)
