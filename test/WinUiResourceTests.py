"""Phase 17 resource validation and UI resource coverage."""
from pathlib import Path
import json,sys,tempfile,shutil,unittest,re
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import embed_languages as compiler
ROOT=Path(__file__).resolve().parents[1]
class Resources(unittest.TestCase):
 def test_embedded_locales(self):self.assertEqual({x['locale'] for x in compiler.validate(ROOT/'config/languages')},{'en-US','pt-BR','es-ES'})
 def test_static_ui_keys_exist(self):
  entries=compiler.validate(ROOT/'config/languages')[0]['translations'];source=(ROOT/'src/winui/main.cpp').read_text('utf-8')
  keys=re.findall(r'(?:tr|label|button|input)\("([a-z][a-zA-Z0-9_.]+)"',source)
  for key in keys:
   if not key.endswith("."):self.assertIn(key,entries)
 def mutate(self,change):
  with tempfile.TemporaryDirectory() as d:
   folder=Path(d);shutil.copytree(ROOT/'config/languages',folder,dirs_exist_ok=True);p=folder/'es-ES.json';j=json.loads(p.read_text('utf-8'));change(j);p.write_text(json.dumps(j),'utf-8')
   with self.assertRaises((ValueError,TypeError,StopIteration)):compiler.validate(folder)
 def test_missing_key(self):self.mutate(lambda j:j['translations'].pop('nav.scan'))
 def test_wrong_placeholder(self):self.mutate(lambda j:j['translations'].update({'scan.completed':'wrong {other}'}))
 def test_bad_plural(self):self.mutate(lambda j:j['translations'].update({'scan.files':{'one':'one'}}))
 def test_empty_label(self):self.mutate(lambda j:j['translations'].update({'nav.scan':''}))
 def test_unknown_locale(self):self.mutate(lambda j:j.update(locale='xx-XX'))
 def test_duplicate_json_key(self):
  with self.assertRaises(ValueError):json.loads('{"x":1,"x":2}',object_pairs_hook=compiler.unique_object)
if __name__=='__main__':unittest.main()
