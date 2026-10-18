{
  "patcher": {
    "fileversion": 1,
    "appversion": {
      "major": 9,
      "minor": 1,
      "revision": 5,
      "architecture": "x64",
      "modernui": 1
    },
    "classnamespace": "box",
    "rect": [
      0,
      0,
      580,
      280
    ],
    "openinpresentation": 1,
    "boxes": [
      {
        "box": {
          "id": "obj-1",
          "maxclass": "newobj",
          "text": "plugin~",
          "patching_rect": [
            20,
            20,
            90,
            22
          ],
          "numinlets": 1,
          "numoutlets": 2,
          "outlettype": [
            "signal",
            "signal"
          ]
        }
      },
      {
        "box": {
          "id": "obj-2",
          "maxclass": "newobj",
          "text": "brknam_probe~",
          "patching_rect": [
            20,
            105,
            130,
            22
          ],
          "numinlets": 1,
          "numoutlets": 2,
          "outlettype": [
            "signal",
            ""
          ]
        }
      },
      {
        "box": {
          "id": "obj-3",
          "maxclass": "newobj",
          "text": "plugout~",
          "patching_rect": [
            20,
            200,
            90,
            22
          ],
          "numinlets": 2,
          "numoutlets": 2,
          "outlettype": [
            "signal",
            "signal"
          ]
        }
      },
      {
        "box": {
          "id": "obj-4",
          "maxclass": "message",
          "text": "ping",
          "patching_rect": [
            200,
            55,
            58,
            22
          ]
        }
      },
      {
        "box": {
          "id": "obj-5",
          "maxclass": "newobj",
          "text": "print BRKNAM_PROBE",
          "patching_rect": [
            200,
            155,
            158,
            22
          ],
          "numinlets": 1,
          "numoutlets": 0
        }
      },
      {
        "box": {
          "id": "obj-6",
          "maxclass": "comment",
          "text": "BRKNAM probe: LEFT through C++ module; RIGHT dry reference.",
          "patching_rect": [
            175,
            20,
            360,
            22
          ]
        }
      },
      {
        "box": {
          "id": "obj-7",
          "maxclass": "comment",
          "text": "Auto-ping when Live device initializes; inspect Max log for BRKNAM_PROBE: ready.",
          "patching_rect": [
            175,
            205,
            390,
            28
          ]
        }
      },
      {
        "box": {
          "id": "obj-8",
          "maxclass": "newobj",
          "text": "live.thisdevice",
          "patching_rect": [
            200,
            100,
            126,
            22
          ],
          "numinlets": 1,
          "numoutlets": 3,
          "outlettype": [
            "bang",
            "int",
            "int"
          ]
        }
      }
    ],
    "lines": [
      {
        "patchline": {
          "source": [
            "obj-1",
            0
          ],
          "destination": [
            "obj-2",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-1",
            1
          ],
          "destination": [
            "obj-3",
            1
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-2",
            0
          ],
          "destination": [
            "obj-3",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-2",
            1
          ],
          "destination": [
            "obj-5",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-4",
            0
          ],
          "destination": [
            "obj-2",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-8",
            0
          ],
          "destination": [
            "obj-4",
            0
          ]
        }
      }
    ],
    "openrect": [
      0,
      0,
      580,
      250
    ],
    "openrectmode": 0,
    "default_fontsize": 10,
    "default_fontname": "Arial Bold",
    "project": {
      "version": 1,
      "creationdate": 3590052493,
      "modificationdate": 3590052493,
      "viewrect": [
        0,
        0,
        580,
        280
      ],
      "autoorganize": 1,
      "hideprojectwindow": 1,
      "showdependencies": 1,
      "autolocalize": 0,
      "contents": {
        "patchers": {}
      },
      "layout": {},
      "searchpath": {},
      "detailsvisible": 0,
      "amxdtype": 1633771873,
      "readonly": 0,
      "devpathtype": 0,
      "devpath": ".",
      "sortmode": 0,
      "viewmode": 0,
      "includepackages": 0
    },
    "latency": 0,
    "is_mpe": 0,
    "external_mpe_tuning_enabled": 0,
    "minimum_live_version": "",
    "minimum_max_version": "",
    "platform_compatibility": 0,
    "autosave": 0
  }
}
