{
  "patcher": {
    "fileversion": 1,
    "appversion": {
      "major": 9,
      "minor": 0,
      "revision": 0,
      "architecture": "x64"
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
          "text": "plugin~ 2",
          "patching_rect": [
            20,
            20,
            90,
            22
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
          ]
        }
      },
      {
        "box": {
          "id": "obj-3",
          "maxclass": "newobj",
          "text": "plugout~ 2",
          "patching_rect": [
            20,
            200,
            90,
            22
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
          ]
        }
      },
      {
        "box": {
          "id": "obj-6",
          "maxclass": "comment",
          "text": "BRKNAM native Linux external smoke test. Test with low monitor volume.",
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
          "text": "Audio L: probe passthrough. Audio R: bypass. Press ping to test Max external.",
          "patching_rect": [
            175,
            205,
            390,
            28
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
      }
    ]
  }
}
