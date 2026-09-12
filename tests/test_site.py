import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1] / 'tools'))
import build_site

class SiteBoundaries(unittest.TestCase):
    def test_cannot_replace_source_or_arbitrary_existing_folder(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'firmware';source.mkdir();sentinel=source/'keep.cpp';sentinel.write_text('keep')
            output=root/'build'/'valuable';output.mkdir(parents=True);(output/'keep').write_text('keep')
            with patch.object(build_site,'ROOT',root):
                for path in (root,source,root/'build',output):
                    with self.assertRaises(ValueError):build_site.build_site(root/'media',path)
            self.assertEqual(sentinel.read_text(),'keep');self.assertEqual((output/'keep').read_text(),'keep')
    def test_rejects_modified_screenshot(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'native.png').write_bytes(b'changed');(root/'large.png').write_bytes(b'large')
            entry={'id':'radio','native_png':'native.png','scaled_png':'large.png','native_sha256':hashlib.sha256(b'original').hexdigest(),'scaled_sha256':hashlib.sha256(b'large').hexdigest()}
            with self.assertRaises(ValueError):build_site.public_screens(root,root/'out',{'screens':[entry]})

if __name__=='__main__':unittest.main()
