/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "sbus.h"

#include "edgetx.h"
#include "timers_driver.h"

#define SBUS_FRAME_SIZE 25
#define SBUS_START_BYTE 0x0F
#define SBUS_END_BYTE 0x00
#define SBUS_FLAGS_IDX 23
#define SBUS_FRAMELOST_BIT 2
#define SBUS_FAILSAFE_BIT 3
#define SBUS_GX12_TRAINER_MASK_MARKER_BIT 4
#define SBUS_GX12_TRAINER_MASK_RIGHT_BIT 5
#define SBUS_GX12_TRAINER_MASK_LEFT_BIT 6
#define SBUS_GX12_TRAINER_RESOLUTION_2X_BIT 7

#define SBUS_CH_BITS 11
#define SBUS_CH_MASK ((1 << SBUS_CH_BITS) - 1)

#define SBUS_CH_CENTER 0x3E0

static const etx_serial_driver_t* _sbus_drv = nullptr;
static void* _sbus_ctx = nullptr;
static bool _sbus_aux_enabled = false;
static uint8_t _sbus_aux_frame[SBUS_FRAME_SIZE];
static uint8_t _sbus_aux_frame_pos = 0;

static void sbusProcessFrame(int16_t* pulses, uint8_t* sbus, uint32_t size);

static int32_t sbusRoundScaleSigned(int32_t value, int32_t numerator,
                                    int32_t denominator)
{
  int32_t magnitude = value < 0 ? -value : value;
  int32_t scaled = (magnitude * numerator + (denominator / 2)) / denominator;
  return value < 0 ? -scaled : scaled;
}

static int16_t sbusLimitResx(int32_t value)
{
  if (value > RESX) return RESX;
  if (value < -RESX) return -RESX;
  return value;
}

void sbusSetReceiveCtx(void* ctx, const etx_serial_driver_t* drv)
{
  _sbus_ctx = ctx;
  _sbus_drv = drv;
  _sbus_aux_frame_pos = 0;
}

void sbusAuxFrameReceived(void*)
{
  if (!_sbus_aux_enabled) return;
  sbusFrameReceived(nullptr);
}

void sbusAuxReceiveData(uint8_t* data, uint32_t size)
{
  if (!_sbus_aux_enabled) return;

  for (uint32_t i = 0; i < size; i++) {
    uint8_t byte = data[i];

    if (_sbus_aux_frame_pos == 0 && byte != SBUS_START_BYTE) {
      continue;
    }

    _sbus_aux_frame[_sbus_aux_frame_pos++] = byte;

    if (_sbus_aux_frame_pos == SBUS_FRAME_SIZE) {
      sbusProcessFrame(trainerInput, _sbus_aux_frame, SBUS_FRAME_SIZE);
      _sbus_aux_frame_pos = 0;
    }
  }
}

void sbusAuxSetEnabled(bool enabled) { _sbus_aux_enabled = enabled; }

void sbusFrameReceived(void*)
{
  if (!_sbus_drv || !_sbus_ctx || !_sbus_drv->copyRxBuffer ||
      !_sbus_drv->getBufferedBytes)
    return;

  if (_sbus_drv->getBufferedBytes(_sbus_ctx) != SBUS_FRAME_SIZE) {
    _sbus_drv->clearRxBuffer(_sbus_ctx);
    return;
  }

  uint8_t frame[SBUS_FRAME_SIZE];
  int received = _sbus_drv->copyRxBuffer(_sbus_ctx, frame, SBUS_FRAME_SIZE);
  if (received < 0) return;

  sbusProcessFrame(trainerInput, frame, received);
}

// Range for pulses (ppm input) is [-512:+512]
static void sbusProcessFrame(int16_t* pulses, uint8_t* sbus, uint32_t size)
{
  if (size != SBUS_FRAME_SIZE || sbus[0] != SBUS_START_BYTE ||
      sbus[SBUS_FRAME_SIZE - 1] != SBUS_END_BYTE) {
    return;  // not a valid SBUS frame
  }
  if ((sbus[SBUS_FLAGS_IDX] & (1 << SBUS_FAILSAFE_BIT)) ||
      (sbus[SBUS_FLAGS_IDX] & (1 << SBUS_FRAMELOST_BIT))) {
    return;  // SBUS invalid frame or failsafe mode
  }
  uint16_t trainerActiveMask = 0xffff;
  uint16_t trainerHighResolutionMask = 0x0000;
  const uint8_t flags = sbus[SBUS_FLAGS_IDX];
  const bool gx12TrainerFrame =
      flags & (1 << SBUS_GX12_TRAINER_MASK_MARKER_BIT);
  if (gx12TrainerFrame) {
    trainerActiveMask = 0xfff0;
    if (flags & (1 << SBUS_GX12_TRAINER_MASK_RIGHT_BIT)) {
      trainerActiveMask |= 0x0003;
    }
    if (flags & (1 << SBUS_GX12_TRAINER_MASK_LEFT_BIT)) {
      trainerActiveMask |= 0x000c;
    }
    if (flags & (1 << SBUS_GX12_TRAINER_RESOLUTION_2X_BIT)) {
      trainerHighResolutionMask = trainerActiveMask;
    }
  }

  sbus++;  // skip start byte

  uint32_t inputbitsavailable = 0;
  uint32_t inputbits = 0;
  for (uint32_t i = 0; i < MAX_TRAINER_CHANNELS; i++) {
    while (inputbitsavailable < SBUS_CH_BITS) {
      inputbits |= *sbus++ << inputbitsavailable;
      inputbitsavailable += 8;
    }
    int32_t delta = (int32_t)(inputbits & SBUS_CH_MASK) - SBUS_CH_CENTER;
    if (trainerHighResolutionMask) {
      *pulses++ = sbusLimitResx(sbusRoundScaleSigned(delta, 5, 4));
    }
    else {
      *pulses++ = delta * 5 / 8;
    }
    inputbitsavailable -= SBUS_CH_BITS;
    inputbits >>= SBUS_CH_BITS;
  }

  trainerNoteSbusFrame();
  trainerSetActiveMask(trainerActiveMask);
  trainerSetHighResolutionMask(trainerHighResolutionMask);
  trainerResetTimer();
}
