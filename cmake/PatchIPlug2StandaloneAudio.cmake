# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
#
# BRKNAM pins iPlug2 to a reviewed commit. That commit has three standalone
# audio-host defects that are visible with a one-channel CoreAudio input:
# - channel 1 is added only to the right-input combo box;
# - an unavailable saved sample rate leaves the combo box with no selection;
# - InitAudio ignores the selected first input/output channels.
#
# Keep the upstream dependency immutable and apply a small, asserted,
# idempotent source patch inside the FetchContent worktree at configure time.

if(NOT DEFINED BRKNAM_IPLUG2_SOURCE_DIR)
  message(FATAL_ERROR "BRKNAM_IPLUG2_SOURCE_DIR is required")
endif()

function(brknam_replace_once file_path old_text new_text marker)
  file(READ "${file_path}" contents)
  string(FIND "${contents}" "${marker}" marker_position)
  if(NOT marker_position EQUAL -1)
    return()
  endif()

  string(FIND "${contents}" "${old_text}" old_position)
  if(old_position EQUAL -1)
    message(FATAL_ERROR
      "Pinned iPlug2 source no longer matches the BRKNAM standalone-audio patch: ${file_path}")
  endif()

  string(REPLACE "${old_text}" "${new_text}" contents "${contents}")
  file(WRITE "${file_path}" "${contents}")
endfunction()

set(dialog_file
    "${BRKNAM_IPLUG2_SOURCE_DIR}/IPlug/APP/IPlugAPP_dialog.cpp")
set(host_file
    "${BRKNAM_IPLUG2_SOURCE_DIR}/IPlug/APP/IPlugAPP_host.cpp")

brknam_replace_once(
  "${dialog_file}"
[=[void IPlugAPPHost::PopulateSampleRateList(HWND hwndDlg, RtAudio::DeviceInfo* inputDevInfo, RtAudio::DeviceInfo* outputDevInfo)
{
  WDL_String buf;

  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_SR,CB_RESETCONTENT,0,0);

  std::vector<int> matchedSRs;

  for (int i=0; i<inputDevInfo->sampleRates.size(); i++)
  {
    for (int j=0; j<outputDevInfo->sampleRates.size(); j++)
    {
      if (inputDevInfo->sampleRates[i] == outputDevInfo->sampleRates[j])
        matchedSRs.push_back(inputDevInfo->sampleRates[i]);
    }
  }

  for (int k=0; k<matchedSRs.size(); k++)
  {
    buf.SetFormatted(20, "%i", matchedSRs[k]);
    SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_SR,CB_ADDSTRING,0,(LPARAM)buf.Get());
    SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_SR,CB_SETITEMDATA,k,(LPARAM)matchedSRs[k]);
  }
  
  WDL_String str;
  str.SetFormatted(32, "%i", mState.mAudioSR);

  LRESULT sridx = SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_SR, CB_FINDSTRINGEXACT, -1, (LPARAM) str.Get());
  SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_SR, CB_SETCURSEL, sridx, 0);
}]=]
[=[void IPlugAPPHost::PopulateSampleRateList(HWND hwndDlg, RtAudio::DeviceInfo* inputDevInfo, RtAudio::DeviceInfo* outputDevInfo)
{
  WDL_String buf;

  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_SR,CB_RESETCONTENT,0,0);

  std::vector<int> matchedSRs;

  for (int i=0; i<inputDevInfo->sampleRates.size(); i++)
  {
    for (int j=0; j<outputDevInfo->sampleRates.size(); j++)
    {
      if (inputDevInfo->sampleRates[i] == outputDevInfo->sampleRates[j])
        matchedSRs.push_back(inputDevInfo->sampleRates[i]);
    }
  }

  for (int k=0; k<matchedSRs.size(); k++)
  {
    buf.SetFormatted(20, "%i", matchedSRs[k]);
    SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_SR,CB_ADDSTRING,0,(LPARAM)buf.Get());
    SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_SR,CB_SETITEMDATA,k,(LPARAM)matchedSRs[k]);
  }

  WDL_String str;
  str.SetFormatted(32, "%i", mState.mAudioSR);

  LRESULT sridx = SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_SR, CB_FINDSTRINGEXACT, -1, (LPARAM) str.Get());
  if (sridx == -1 && !matchedSRs.empty())
  {
    sridx = 0;
    for (int k = 0; k < matchedSRs.size(); ++k)
    {
      if (matchedSRs[k] == 48000)
      {
        sridx = k;
        break;
      }
    }
    mState.mAudioSR = matchedSRs[static_cast<std::size_t>(sridx)];
  }
  SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_SR, CB_SETCURSEL, sridx, 0);
}]=]
  "mState.mAudioSR = matchedSRs[static_cast<std::size_t>(sridx)]")

brknam_replace_once(
  "${dialog_file}"
[=[void IPlugAPPHost::PopulateAudioInputList(HWND hwndDlg, RtAudio::DeviceInfo* info)
{
  WDL_String buf;

  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_L,CB_RESETCONTENT,0,0);
  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_R,CB_RESETCONTENT,0,0);

  int i;

  for (i=0; i<info->inputChannels -1; i++)
  {
    buf.SetFormatted(20, "%i", i+1);
    SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_L,CB_ADDSTRING,0,(LPARAM)buf.Get());
    SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_R,CB_ADDSTRING,0,(LPARAM)buf.Get());
  }

  // TEMP
  buf.SetFormatted(20, "%i", i+1);
  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_R,CB_ADDSTRING,0,(LPARAM)buf.Get());

  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_L,CB_SETCURSEL, mState.mAudioInChanL - 1, 0);
  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_R,CB_SETCURSEL, mState.mAudioInChanR - 1, 0);
}]=]
[=[void IPlugAPPHost::PopulateAudioInputList(HWND hwndDlg, RtAudio::DeviceInfo* info)
{
  WDL_String buf;

  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_L,CB_RESETCONTENT,0,0);
  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_R,CB_RESETCONTENT,0,0);

  for (int i = 0; i < info->inputChannels; ++i)
  {
    buf.SetFormatted(20, "%i", i + 1);
    SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_L,CB_ADDSTRING,0,(LPARAM)buf.Get());
    SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_R,CB_ADDSTRING,0,(LPARAM)buf.Get());
  }

  const auto lastChannel = static_cast<uint32_t>(info->inputChannels);
  mState.mAudioInChanL = std::max(1U, std::min(mState.mAudioInChanL, lastChannel));
  mState.mAudioInChanR = std::max(1U, std::min(mState.mAudioInChanR, lastChannel));

  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_L,CB_SETCURSEL, mState.mAudioInChanL - 1, 0);
  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_R,CB_SETCURSEL, mState.mAudioInChanR - 1, 0);
}]=]
  "const auto lastChannel = static_cast<uint32_t>(info->inputChannels)")

brknam_replace_once(
  "${host_file}"
[=[  RtAudio::StreamParameters iParams, oParams;
  iParams.deviceId = inID;
  iParams.nChannels = GetPlug()->MaxNChannels(ERoute::kInput); // TODO: flexible channel count
  iParams.firstChannel = 0; // TODO: flexible channel count

  oParams.deviceId = outID;
  oParams.nChannels = GetPlug()->MaxNChannels(ERoute::kOutput); // TODO: flexible channel count
  oParams.firstChannel = 0; // TODO: flexible channel count
]=]
[=[  RtAudio::StreamParameters iParams, oParams;
  const auto inputDevInfo = mDAC->getDeviceInfo(inID);
  const auto outputDevInfo = mDAC->getDeviceInfo(outID);

  iParams.deviceId = inID;
  iParams.nChannels = GetPlug()->MaxNChannels(ERoute::kInput);
  const auto maximumInputFirstChannel =
    inputDevInfo.inputChannels > iParams.nChannels
      ? inputDevInfo.inputChannels - iParams.nChannels
      : 0U;
  const auto requestedInputFirstChannel =
    mState.mAudioInChanL > 0 ? mState.mAudioInChanL - 1 : 0U;
  iParams.firstChannel = std::min(requestedInputFirstChannel, maximumInputFirstChannel);

  oParams.deviceId = outID;
  oParams.nChannels = GetPlug()->MaxNChannels(ERoute::kOutput);
  const auto maximumOutputFirstChannel =
    outputDevInfo.outputChannels > oParams.nChannels
      ? outputDevInfo.outputChannels - oParams.nChannels
      : 0U;
  const auto requestedOutputFirstChannel =
    mState.mAudioOutChanL > 0 ? mState.mAudioOutChanL - 1 : 0U;
  oParams.firstChannel = std::min(requestedOutputFirstChannel, maximumOutputFirstChannel);
]=]
  "maximumInputFirstChannel")

message(STATUS "Applied BRKNAM standalone-audio fixes to pinned iPlug2")
