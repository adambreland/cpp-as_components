uint32_t fcgi_synchronous_interface::
ExtractFourByteLength(const uint8_t* content_ptr) const
{
  uint32_t length {*content_ptr & 0x7f}; // mask out leading 1;
  // Perform three shifts by 8 bits to extract all four bytes.
  for(char i {0}; i < 3; i++)
  {
    length <<= 8;
    content_ptr++;
    length += *content_ptr;
  }
  return length;
}

void fcgi_synchronous_interface::
EncodeFourByteLength(uint32_t length, std::basic_string<uint8_t>* string_ptr)
{
  for(char i {0}; i < 4; i++)
  {
    string_ptr->push_back(length >> (24 - (8*i)));
  }
}

std::vector<std::pair<std::basic_string<uint8_t>, std::basic_string<uint8_t>>>
fcgi_synchronous_interface::
ProcessBinaryNameValuePairs(int content_length, const uint8_t* content_ptr)
{
  int bytes_processed {0};
  std::vector<std::pair<std::basic_string<uint8_t>, std::basic_string<uint8_t>>>
  result {};
  std::vector<std::pair<std::basic_string<uint8_t>, std::basic_string<uint8_t>>>
  error_result {};

  while(bytes_processed < content_length)
  {
    uint32_t name_length;
    uint32_t value_length;
    bool name_length_bit = *content_ptr >> 7;
    bool value_length_bit;

    // Extract name length.
    if(name_length_bit)
    {
      if((bytes_processed + 3) > content_length)
        return error_result; // Not enough information to continue.
      name_length = ExtractFourByteLength(content_ptr);
      bytes_processed += 4;
      content_ptr += 4;
    }
    else
    {
      name_length = *content_ptr;
      bytes_processed += 1;
      content_ptr += 1;
    }

    // Extract value length.
    if((bytes_processed + 1) > content_length)
      return error_result;
    value_length_bit = *content_ptr >> 7;
    if(value_length_bit)
    {
      if((bytes_processed + 3) > content_length)
        return error_result; // Not enough information to continue.
      value_length = ExtractFourByteLength(content_ptr);
      bytes_processed += 4;
      content_ptr += 4;
    }
    else
    {
      value_length = *content_ptr;
      bytes_processed += 1;
      content_ptr += 1;
    }

    // Extract name and value as byte strings.
    if((bytes_processed + name_length + value_length) > content_length)
      return error_result; // Not enough information to continue.
    std::basic_string<uint8_t> name {content_ptr, name_length};
    content_ptr += name_length;
    std::basic_string<uint8_t> value {content_ptr, value_length};
    content_ptr += value_length;
    bytes_processed += (name_length + value_length);
    result.emplace_back(std::move(name), std::move(value));
  } // End while (no more pairs to process).

  return result;
}
