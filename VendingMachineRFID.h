#ifndef _VendingMachineRFID_h_
#define _VendingMachineRFID_h_
/*
 * Helper struct, as Arduino does not support typedef in .ino files
 */

typedef uint32_t card_uid_t;
typedef uint16_t card_credits_t;

typedef struct {
  card_uid_t uid;
  card_credits_t credits;
} card_t;

#endif
