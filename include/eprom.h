

void initWire();
void i2c_eeprom_write_byte( int deviceaddress, unsigned int eeaddress, byte data );
byte i2c_eeprom_read_byte( int deviceaddress, unsigned int eeaddress );
void eepromWriteData(float value);
float eepromReadSavedConsigne();