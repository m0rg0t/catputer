import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1] / 'tools'))
import build_site

class SiteBoundaries(unittest.TestCase):
    def test_sunny_favorite_requires_current_schema(self):
        for schema, mood, valid in ((5, 3, True), (4, 3, False), (5, 4, False), (4, 2, True)):
            code=f'lofi{schema}-000000000ca7cafe-{mood}-1-78-18-0-0-0-3-0'
            self.assertEqual(build_site.parse_favorite_seed({'favorite_code':code}),
                             '000000000CA7CAFE' if valid else None)

    def test_day_animation_is_hash_bound_and_optional(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); output=root/'out'
            manifest={}
            for key, name in (('contact_sheet','contact.png'), ('animation','scene.gif'), ('day_animation','scene-day.gif')):
                (root/name).write_bytes(name.encode())
                manifest[key]={'path' if key=='contact_sheet' else 'gif':name,
                               'sha256':hashlib.sha256(name.encode()).hexdigest()}
            with patch.object(build_site,'OUTPUT_ROOT',output):
                copied=build_site.copy_common_media(root,output,manifest)
                self.assertEqual((output/copied['day_animation']).read_bytes(),b'scene-day.gif')
                (root/'scene-day.gif').write_bytes(b'stale')
                with self.assertRaisesRegex(ValueError,'daytime animation hash mismatch'):
                    build_site.copy_common_media(root,output,manifest)
                del manifest['day_animation']
                self.assertNotIn('day_animation',build_site.copy_common_media(root,output,manifest))

    def test_favorite_seed_with_manual_tempo(self):
        base='lofi3-000000000ca7cafe-2-1-78-18'
        for suffix in ('', '-40', '-120', '-180'):
            self.assertEqual(build_site.parse_favorite_seed({'favorite_code':base+suffix}), '000000000CA7CAFE')
        for suffix in ('-0', '-39', '-181', '-65536', '-120-1', '-abc'):
            self.assertIsNone(build_site.parse_favorite_seed({'favorite_code':base+suffix}))

    def test_current_favorite_tones_and_meter(self):
        base='lofi5-000000000ca7cafe-2-1-78-18'
        for suffix in ('-0-0-0-3-0', '-40-1-5-0-2', '-180-3-2-1-1'):
            self.assertEqual(build_site.parse_favorite_seed({'favorite_code':base+suffix}), '000000000CA7CAFE')
        for suffix in ('', '-0', '-39-0-0-3-0', '-0-4-0-3-0', '-0-0-6-3-0', '-0-0-0-6-0', '-0-0-0-3-3'):
            self.assertIsNone(build_site.parse_favorite_seed({'favorite_code':base+suffix}))

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

    def test_new_audio_uses_new_urls_and_matches_current_schema(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);audio=root/'build/audio';audio.mkdir(parents=True)
            (root/'VERSION').write_text('0.1.2-dev\n')
            header=root/'firmware/include/lofi/music.h';header.parent.mkdir(parents=True)
            header.write_text('constexpr std::uint32_t kMusicSchemaVersion = 3;\nconstexpr std::uint8_t kMusicVoiceCapacity = 12;\n')
            output_patch=patch.object(build_site,'OUTPUT_ROOT',root/'build/site')
            output_patch.start();self.addCleanup(output_patch.stop)
            def metadata(engine):
                return {'version':'0.1.2-dev','mood':'Night','engine':engine.title(),
                        'favorite_code':f'lofi3-000000000ca7cafe-2-{0 if engine=="synth" else 1}-78-18',
                        'generation_schema':3,'score_hash':'abc123','frames':2880000,'sample_rate':32000,
                        'mp3_sha256':hashlib.sha256(b'original audio').hexdigest()}
            for engine in ('synth','hybrid'):
                (audio/f'night-{engine}.mp3').write_bytes(b'original audio')
                (audio/f'night-{engine}.json').write_text(json.dumps(metadata(engine)))
            old=next(pair for pair in build_site.copy_audio(root,root/'build/site') if pair['mood']=='night')
            self.assertEqual(old['matched_seed'],'000000000CA7CAFE')
            (audio/'night-synth.mp3').write_bytes(b'new composition')
            with self.assertRaises(ValueError):build_site.copy_audio(root,root/'build/site')
            revised=metadata('synth');revised['mp3_sha256']=hashlib.sha256(b'new composition').hexdigest()
            (audio/'night-synth.json').write_text(json.dumps(revised))
            new=next(pair for pair in build_site.copy_audio(root,root/'build/site') if pair['mood']=='night')
            self.assertNotEqual(old['engines'][0]['path'],new['engines'][0]['path'])
            for entry in new['engines']:
                entry['frames']=5760000
            self.assertIn('0:00 / 3:00',build_site.render_audio([new]))
            for key,value in (('generation_schema',None),('generation_schema',1),('version','0.1.0-dev'),
                              ('mood','Cozy'),('engine','Synth'),('frames',1000),('mp3_sha256','0'*64)):
                with self.subTest(key=key,value=value):
                    invalid=metadata('hybrid');invalid[key]=value
                    (audio/'night-hybrid.json').write_text(json.dumps(invalid))
                    with self.assertRaises(ValueError):build_site.copy_audio(root,root/'build/site')

if __name__=='__main__':unittest.main()
