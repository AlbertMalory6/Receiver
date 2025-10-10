#!/usr/bin/env python3
"""
CRC-8 Testing Utility
Tests the CRC implementation against known test vectors
"""

def calculate_crc8(data_bits, polynomial=0xD7):
    """
    Python implementation of CRC-8 (should match C++ version)
    
    Args:
        data_bits: list of bool or string of '0'/'1'
        polynomial: CRC polynomial (default 0xD7)
    
    Returns:
        8-bit CRC value
    """
    if isinstance(data_bits, str):
        data_bits = [c == '1' for c in data_bits]
    
    crc = 0
    for bit in data_bits:
        crc ^= (0x80 if bit else 0x00)
        for _ in range(8):
            if crc & 0x80:
                crc = (crc << 1) ^ polynomial
            else:
                crc <<= 1
        crc &= 0xFF  # Keep 8 bits
    
    return crc

def crc_to_bits(crc_byte):
    """Convert CRC byte to 8-bit string"""
    return ''.join([str((crc_byte >> (7-i)) & 1) for i in range(8)])

def test_crc_basic():
    """Test CRC with known vectors"""
    print("="*70)
    print("CRC-8 BASIC TESTS")
    print("="*70)
    
    test_cases = [
        ("00000000", "All zeros (8 bits)"),
        ("11111111", "All ones (8 bits)"),
        ("10101010", "Alternating pattern"),
        ("01010101", "Alternating pattern (inverted)"),
        ("00000001", "Single bit set"),
        ("10000000", "MSB set"),
        ("0" * 100, "100 zeros"),
        ("1" * 100, "100 ones"),
        ("01" * 50, "100 bits alternating"),
    ]
    
    for data, description in test_cases:
        crc = calculate_crc8(data)
        crc_bits = crc_to_bits(crc)
        print(f"\n{description}:")
        print(f"  Data:     {data[:40]}{'...' if len(data) > 40 else ''} ({len(data)} bits)")
        print(f"  CRC-8:    0x{crc:02X} = {crc_bits}")

def test_error_detection():
    """Test CRC's ability to detect errors"""
    print("\n" + "="*70)
    print("CRC ERROR DETECTION TEST")
    print("="*70)
    
    # Original data
    original = "10110100" * 10  # 80 bits
    original_crc = calculate_crc8(original)
    
    print(f"\nOriginal data: {original[:40]}... ({len(original)} bits)")
    print(f"Original CRC:  0x{original_crc:02X}")
    
    # Test single-bit errors
    print("\n--- Single Bit Error Detection ---")
    errors_detected = 0
    for i in range(len(original)):
        corrupted = list(original)
        corrupted[i] = '0' if corrupted[i] == '1' else '1'
        corrupted = ''.join(corrupted)
        corrupted_crc = calculate_crc8(corrupted)
        
        if corrupted_crc != original_crc:
            errors_detected += 1
        else:
            print(f"  ⚠️  Bit {i} error NOT detected! (CRC collision)")
    
    print(f"Single-bit errors detected: {errors_detected}/{len(original)} ({100*errors_detected/len(original):.1f}%)")
    
    # Test burst errors
    print("\n--- Burst Error Detection ---")
    burst_sizes = [2, 3, 4, 5, 8, 16]
    for burst_size in burst_sizes:
        detected = 0
        total_tests = min(50, len(original) - burst_size)
        
        for start_pos in range(0, len(original) - burst_size, max(1, len(original) // total_tests)):
            corrupted = list(original)
            for i in range(start_pos, start_pos + burst_size):
                corrupted[i] = '0' if corrupted[i] == '1' else '1'
            corrupted = ''.join(corrupted)
            corrupted_crc = calculate_crc8(corrupted)
            
            if corrupted_crc != original_crc:
                detected += 1
        
        print(f"  {burst_size}-bit bursts: {detected}/{total_tests} detected ({100*detected/total_tests:.1f}%)")

def test_file_crc(input_file="INPUT.txt"):
    """Test CRC on actual INPUT.txt file"""
    import os
    
    print("\n" + "="*70)
    print("FILE CRC TEST")
    print("="*70)
    
    if not os.path.exists(input_file):
        print(f"\n❌ {input_file} not found. Skipping file test.")
        return
    
    with open(input_file, 'r') as f:
        data = f.read().strip()
    
    # Validate format
    if not all(c in '01' for c in data):
        print(f"❌ ERROR: {input_file} contains non-binary characters")
        return
    
    crc = calculate_crc8(data)
    crc_bits = crc_to_bits(crc)
    
    print(f"\nFile: {input_file}")
    print(f"Data length: {len(data)} bits")
    print(f"First 50 bits: {data[:50]}{'...' if len(data) > 50 else ''}")
    print(f"Last 50 bits:  {'...' if len(data) > 50 else ''}{data[-50:]}")
    print(f"\nCalculated CRC: 0x{crc:02X} = {crc_bits}")
    print(f"\nTransmitted frame would be:")
    print(f"  [{len(data)} payload bits] + [{crc_bits}]")
    print(f"  Total: {len(data) + 8} bits")
    
    # Verify by recomputing
    full_frame = data + crc_bits
    print(f"\n--- Verification Test ---")
    print(f"If receiver gets: {full_frame[:40]}...{full_frame[-10:]}")
    
    payload_received = full_frame[:-8]
    crc_received = int(full_frame[-8:], 2)
    crc_calculated = calculate_crc8(payload_received)
    
    print(f"Payload: {payload_received[:40]}... ({len(payload_received)} bits)")
    print(f"Received CRC:   0x{crc_received:02X}")
    print(f"Calculated CRC: 0x{crc_calculated:02X}")
    print(f"Match: {'✅ YES' if crc_received == crc_calculated else '❌ NO'}")

def test_receiver_output(output_file="OUTPUT.txt", input_file="INPUT.txt"):
    """Compare receiver output with original input"""
    import os
    
    print("\n" + "="*70)
    print("RECEIVER OUTPUT VALIDATION")
    print("="*70)
    
    if not os.path.exists(output_file):
        print(f"\n❌ {output_file} not found. Run receiver first.")
        return
    
    if not os.path.exists(input_file):
        print(f"\n❌ {input_file} not found.")
        return
    
    with open(input_file, 'r') as f:
        input_data = f.read().strip()
    
    with open(output_file, 'r') as f:
        output_data = f.read().strip()
    
    print(f"\nINPUT.txt:  {len(input_data)} bits")
    print(f"OUTPUT.txt: {len(output_data)} bits")
    
    if len(input_data) != len(output_data):
        print(f"\n⚠️  LENGTH MISMATCH!")
        print(f"   Expected: {len(input_data)} bits")
        print(f"   Received: {len(output_data)} bits")
        print(f"   Difference: {abs(len(input_data) - len(output_data))} bits")
    
    # Compare bit by bit
    errors = []
    min_len = min(len(input_data), len(output_data))
    
    for i in range(min_len):
        if input_data[i] != output_data[i]:
            errors.append(i)
    
    if not errors and len(input_data) == len(output_data):
        print(f"\n✅ PERFECT MATCH! All {len(input_data)} bits correct.")
        print("   CRC is working correctly!")
    else:
        print(f"\n❌ ERRORS DETECTED: {len(errors)} bit errors")
        print(f"   BER: {100*len(errors)/min_len:.2f}%")
        
        if errors:
            print(f"\n   First 10 errors at positions:")
            for i, pos in enumerate(errors[:10]):
                print(f"     Bit {pos}: expected '{input_data[pos]}', got '{output_data[pos]}'")
        
        print(f"\n   CRC should have detected this!")
        print(f"   Possible causes:")
        print(f"     1. CRC implementation mismatch between sender/receiver")
        print(f"     2. Errors in CRC bits themselves")
        print(f"     3. Payload size mismatch")

def generate_test_vector():
    """Generate a test vector for manual verification"""
    print("\n" + "="*70)
    print("TEST VECTOR GENERATION")
    print("="*70)
    
    # Simple test case
    test_data = "10110100"
    crc = calculate_crc8(test_data)
    crc_bits = crc_to_bits(crc)
    
    print(f"\nTest Vector for Manual C++ Verification:")
    print(f"  Input:  {test_data}")
    print(f"  CRC:    0x{crc:02X} = {crc_bits}")
    print(f"\nC++ test code:")
    print(f'  std::vector<bool> test = {{{", ".join([str(int(b)) for b in test_data])}}};')
    print(f'  uint8_t crc = FSK::calculateCRC8(test);')
    print(f'  std::cout << "CRC: 0x" << std::hex << (int)crc << std::endl;')
    print(f'  // Expected output: CRC: 0x{crc:x}')

def main():
    print("╔═══════════════════════════════════════════════════════════════════╗")
    print("║            CRC-8 IMPLEMENTATION TESTING UTILITY                   ║")
    print("║                 Polynomial: 0xD7                                  ║")
    print("╚═══════════════════════════════════════════════════════════════════╝")
    
    # Run all tests
    test_crc_basic()
    test_error_detection()
    test_file_crc()
    test_receiver_output()
    generate_test_vector()
    
    print("\n" + "="*70)
    print("TESTING COMPLETE")
    print("="*70)
    print("\nNext steps:")
    print("  1. If file CRC test passed → CRC calculation is correct")
    print("  2. If receiver output validation failed → check synchronization")
    print("  3. Compare C++ output with test vector to verify implementation")

if __name__ == "__main__":
    main()

