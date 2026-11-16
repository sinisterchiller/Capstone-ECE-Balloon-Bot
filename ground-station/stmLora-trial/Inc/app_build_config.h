/**
  ******************************************************************************
  * @file    app_build_config.h
  * @brief   Single-source compile-time configuration for the LoRa Ping-Pong app.
  *
  *          This header is the ONLY place to change role and TX settings for
  *          the Debug preset (APP_CONFIG_FROM_SOURCE=ON).
  *
  *          The other presets (Release, InitiatorTx, ResponderTx) use CMake
  *          cache variables instead (APP_CONFIG_FROM_SOURCE=OFF).
  ******************************************************************************
  */

#ifndef APP_BUILD_CONFIG_H
#define APP_BUILD_CONFIG_H

/**
  * @brief  Board role: 1 = Initiator (sends PING first), 0 = Responder (waits for PING).
  *         Default: 0 (Responder).
  *
  *         For Debug preset: edit this file, then configure/build/flash.
  *         For other presets: use -DAPP_ROLE_INITIATOR=<0|1> in cmake command.
  */
#ifndef APP_ROLE_INITIATOR
#define APP_ROLE_INITIATOR    1
#endif

/**
  * @brief  TX enable guard: 1 = transmit, 0 = receive-only.
  *         Default: 1 (TX enabled for testing).
  *
  *         For Debug preset: edit this file, then configure/build/flash.
  *         For other presets: use -DAPP_TX_ENABLED=<0|1> in cmake command.
  */
#ifndef APP_TX_ENABLED
#define APP_TX_ENABLED        1
#endif

#endif /* APP_BUILD_CONFIG_H */
