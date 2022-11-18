#ifndef __NVM_H__
#define __NVM_H__

bool nvm_read_key(const char *key, uint8_t val[], size_t *len);
bool nvm_write_key(const char *key, uint8_t val[], size_t len);

#endif /* __NVM_H__ */