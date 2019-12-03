
fcgi_synchronous_interface::RequestData::
RequestData(uint16_t role, bool close_connection)
: FCGI_PARAMS_complete_ {false}, FCGI_STDIN_complete_ {false},
  FCGI_DATA_complete_ {false}, role_ {role}, abort_ {false},
  close_connection_ {close_connection},
  RequestStatus {fcgi_synchronous_interface::RequestStatus::kRequestPending}
{}

inline bool fcgi_synchronous_interface::RequestData::get_abort()
{
  return abort_;
}

inline void fcgi_synchronous_interface::RequestData::set_abort()
{
  abort_ = true;
}

inline bool fcgi_synchronous_interface::RequestData::get_close_connection()
{
  return close_connection_;
}

inline uint16_t fcgi_synchronous_interface::RequestData::get_role()
{
  return role_;
}

inline bool fcgi_synchronous_interface::RequestData::IsRequestComplete()
{
  return FCGI_PARAMS_complete_ && FCGI_STDIN_complete_ && FCGI_DATA_complete_;
}

// PARAMS

inline bool fcgi_synchronous_interface::RequestData::get_PARAMS_completion()
{
  return FCGI_PARAMS_complete_;
}

inline void fcgi_synchronous_interface::RequestData::complete_PARAMS()
{
  FCGI_PARAMS_complete_ = true;
}

void fcgi_synchronous_interface::RequestData::
AppendToPARAMS(const uint8_t* buffer_ptr, size count)
{
  FCGI_PARAMS_.append(buffer_ptr, count);
}

// STDIN

inline bool fcgi_synchronous_interface::RequestData::get_STDIN_completion()
{
  return FCGI_STDIN_complete_;
}

inline void fcgi_synchronous_interface::RequestData::complete_STDIN_PARAMS()
{
  FCGI_STDIN_complete_ = true;
}

void fcgi_synchronous_interface::RequestData::
AppendToSTDIN(const uint8_t* buffer_ptr, size count);
{
  FCGI_STDIN_.append(buffer_ptr, count);
}

// DATA

inline bool fcgi_synchronous_interface::RequestData::get_FCGI_DATA_completion()
{
  return FCGI_PARAMS_complete_;
}

inline void fcgi_synchronous_interface::RequestData::complete_DATA_PARAMS()
{
  FCGI_PARAMS_complete_ = true;
}

void fcgi_synchronous_interface::RequestData::
append_to_DATA(const uint8_t* buffer_ptr, size count)
{
  FCGI_DATA_.append(buffer_ptr, count);
}
