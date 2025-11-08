import os

def compare_files(file1_path, file2_path):
    """
    Compares two files containing binary strings (0s and 1s),
    outputs the differing parts, and calculates similarity.
    Handles files of different lengths.
    """
    try:
        # Read content from both files
        with open(file1_path, 'r') as f1:
            # Use strip() to remove leading/trailing whitespace, including newlines
            content1 = f1.read().strip()
            
        with open(file2_path, 'r') as f2:
            content2 = f2.read().strip()

        # --- Validation ---
        if not content1 and not content2:
            print("Error: Both files are empty.")
            return
        
        # Handle one empty file
        if not content1:
            print(f"Error: '{file1_path}' is empty.")
            print(f"'{file2_path}' has {len(content2)} bits.")
            print("Similarity: 0.00%")
            return
            
        if not content2:
            print(f"Error: '{file2_path}' is empty.")
            print(f"'{file1_path}' has {len(content1)} bits.")
            print("Similarity: 0.00%")
            return

        if any(bit not in '01' for bit in content1) or any(bit not in '01' for bit in content2):
            print("Error: Files must contain only 0s and 1s.")
            return
            
        # --- Comparison ---
        len1 = len(content1)
        len2 = len(content2)
        min_len = min(len1, len2)
        max_len = max(len1, len2)
        
        differences = []
        
        # Compare the common part
        for i in range(min_len):
            if content1[i] != content2[i]:
                differences.append({
                    "index": i,
                    file1_path: content1[i],
                    file2_path: content2[i]
                })

        # --- Report Results ---
        
        # Report differences in the common part
        if differences:
            print("Found differences in common segment:")
            for diff in differences:
                print(f"  - At index {diff['index']}:")
                print(f"    {file1_path}: {diff[file1_path]}")
                print(f"    {file2_path}: {diff[file2_path]}")
        else:
            if min_len > 0:
                print(f"No differences found in common segment (up to index {min_len - 1}).")
            else:
                print("No common segment to compare (one file might be empty).")


        print("\n--- Summary ---")
        
        total_bits_to_compare = max_len
        diff_in_common = len(differences)
        diff_in_length = max_len - min_len
        total_different_bits = diff_in_common + diff_in_length
        
        similar_bits = total_bits_to_compare - total_different_bits
        
        if total_bits_to_compare == 0:
             similarity_percentage = 100.0 # Both files were empty and handled above, but as a safeguard.
        else:
             similarity_percentage = (similar_bits / total_bits_to_compare) * 100

        print(f"Longest file bits (total bits for comparison): {total_bits_to_compare}")

        # Report length difference
        if diff_in_length > 0:
            if len1 > len2:
                longer_file = file1_path
                shorter_file = file2_path
            else:
                longer_file = file2_path
                shorter_file = file1_path
                
            print(f"'{longer_file}' is longer by {diff_in_length} bits.")
            print(f"  Data missing from '{shorter_file}' from index {min_len} to {max_len - 1}.")
        else:
            print("Files are of the same length.")

        print(f"\nTotal different bits: {total_different_bits}")
        print(f"  (Differences in common part: {diff_in_common})")
        print(f"  (Difference from extra length: {diff_in_length})")
        
        print(f"\nSimilarity: {similarity_percentage:.2f}%")
        
        if total_different_bits == 0:
            print("\nFiles are identical.")


    except FileNotFoundError as e:
        # Corrected attribute from .fileName to .filename
        print(f"Error: File not found - {e.filename}")
    except Exception as e:
        print(f"An unexpected error occurred: {e}")

if __name__ == "__main__":
    # NOTE: The create_sample_files() function has been removed.
    # You must provide input.txt and output.txt yourself
    # for this script to run.
    
    file1 = "Receiver/input.txt"
    file2 = "Receiver/output.txt"

    print(f"Comparing '{file1}' and '{file2}'...\n")
    compare_files(file1, file2)