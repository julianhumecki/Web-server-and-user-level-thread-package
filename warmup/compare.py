
def reader(filename1, filename2):
    words = dict()
    with open(filename1, 'r') as a_file:
        for line in a_file:
            if line in words:
                words[line] += 1
            else: 
                words[line] = 1
    
    with open(filename2, 'r') as a_file:
        for line in a_file:
            if line in words:
                words[line] -= 1
            else:
                print("Unessen word!!") 
                words[line] = -1
    
    for word in words:
        if words[word] < 0:
            print(f"Word: {word}, Count: {words[word]}")

reader("sorted_dummy.txt", "expected-big.txt")