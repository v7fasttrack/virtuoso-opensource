--
--  $Id$
--
--  This file is part of the OpenLink Software Virtuoso Open-Source (VOS)
--  project.
--
--  Copyright (C) 1998-2009 OpenLink Software
--
--  This project is free software; you can redistribute it and/or modify it
--  under the terms of the GNU General Public License as published by the
--  Free Software Foundation; only version 2 of the License, dated June 1991.
--
--  This program is distributed in the hope that it will be useful, but
--  WITHOUT ANY WARRANTY; without even the implied warranty of
--  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
--  General Public License for more details.
--
--  You should have received a copy of the GNU General Public License along
--  with this program; if not, write to the Free Software Foundation, Inc.,
--  51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
--

create procedure b3s_page_get_type (in val any)
{

  declare delim, delim1, delim2, delim3 integer;
  declare pref, res varchar;

  delim1 := coalesce (strrchr (val, '/'), -1);
  delim2 := coalesce (strrchr (val, '#'), -1);
  delim3 := coalesce (strrchr (val, ':'), -1);
  delim := __max (delim1, delim2, delim3);

  if (delim < 0)
    return val;

  pref := subseq (val, 0, delim+1);

  if (pref = val)
    return val;

  if (strstr (val, 'http://dbpedia.org/resource/') = 0 ) return 'dbpedia';
  if (strstr (val, 'http://dbpedia.org/property/') = 0 ) return 'p';
  if (strstr (val, 'http://dbpedia.openlinksw.com/wikicompany/') = 0 ) return 'wikicompany';
  if (strstr (val, 'http://dbpedia.org/class/yago/') = 0 ) return 'yago';
  if (strstr (val, 'http://www.w3.org/2003/01/geo/wgs84_pos#') = 0 ) return 'geo';
  if (strstr (val, 'http://www.geonames.org/ontology#') = 0 ) return 'geonames';
  if (strstr (val, 'http://xmlns.com/foaf/0.1/') = 0 ) return 'foaf';
  if (strstr (val, 'http://www.w3.org/2004/02/skos/core#') = 0 ) return 'skos';
  if (strstr (val, 'http://www.w3.org/2002/07/owl#') = 0 ) return 'owl';
  if (strstr (val, 'http://www.w3.org/2000/01/rdf-schema#') = 0 ) return 'rdfs';
  if (strstr (val, 'http://www.w3.org/1999/02/22-rdf-syntax-ns#') = 0 ) return 'rdf';
  if (strstr (val, 'http://www.w3.org/2001/XMLSchema#') = 0 ) return 'xsd';
  if (strstr (val, 'http://purl.org/dc/elements/1.1/') = 0 ) return 'dc';
  if (strstr (val, 'http://purl.org/dc/terms/') = 0 ) return 'dcterms';
  if (strstr (val, 'http://dbpedia.org/units/') = 0 ) return 'units';
  if (strstr (val, 'http://www.w3.org/1999/xhtml/vocab#') = 0 ) return 'xhv';
  if (strstr (val, 'http://rdfs.org/sioc/ns#') = 0 ) return 'sioc';
  if (strstr (val, 'http://purl.org/ontology/bibo/') = 0 ) return 'bibo';

  if (pref is null)
    return val;
  res := __xml_get_ns_prefix (pref, 2);
  if (res is null)
    return val;
  return res;
}
;

--
-- make a vector of languages and their quality 
--
create procedure b3s_get_lang_acc (in lines any)
{
  declare accept, itm varchar;
  declare i, l, q int;
  declare ret, arr any;

  accept := 'en';
  if (lines is not null)
    {
      accept := http_request_header_full (lines, 'Accept-Language', 'en');
    }
  arr := split_and_decode (accept, 0, '\0\0,;');
  q := 0;
  l := length (arr);
  ret := make_array (l, 'any');
  for (i := 0; i < l; i := i + 2)
    {
      declare tmp any;
      itm := trim(arr[i]);
      if (itm like '%-%')
	itm := subseq (itm, 0, strchr (itm, '-'));
      q := arr[i+1];
      if (q is null)
	q := 1.0;
      else
	{
	  tmp := split_and_decode (q, 0, '\0\0=');
	  if (length (tmp) = 2)
	    q := atof (tmp[1]);
	  else
	    q := 1.0;
	}
      ret[i] := itm;
      ret[i+1] := q;
    }
  return ret;
}
;

create procedure b3s_str_lang_check (in lang any, in acc any)
{
  if (lang like '%-%')
    lang := subseq (lang, 0, strchr (lang, '-'));
  if (not length (lang))
    return 1;
  else if (position (lang, acc) > 0)
    return 1;
  else if (position ('*', acc) > 0)
    return 1;
  return 0;
}
;

create procedure 
b3s_handle_ses (inout _path any, inout _lines any, inout _params any)
{
   declare sid, refr varchar;

   sid := get_keyword ('sid', _params); 

   if (sid is null) {
     refr := http_request_header (http_request_header (), 'Referer', null, null);

     if (refr is not null)
       {
         declare ht, pars any; 
         ht := WS.WS.PARSE_URI (refr);
         pars := ht[4];
         pars := split_and_decode (pars);
         if (pars is not null) 
         sid := get_keyword ('sid', pars);
       }
   }

   if (sid is not null) connection_set ('sid', sid);
}
;

-- XXX should probably find the most specific if more than one class and inference rule is set

create procedure b3s_type (in subj varchar, 
                           in _from varchar, 
                           out url varchar, 
                           out c_iri varchar)
{
  declare meta, data, ll any;
  ll := 'unknown';
  url := 'javascript:void()';
  if (length (subj))
    {
      exec (sprintf ('sparql select ?l ?tp %s where { <%S> a ?tp . optional { ?tp rdfs:label ?l } }', _from, subj), 
	  null, null, vector (), 100, meta, data);
      if (length (data))
	{
	  if (data[0][0] is not null)
  	    ll := data[0][0];
	  else  
	    ll := b3s_uri_local_part (data[0][1]);
	  url := b3s_http_url (data[0][1]);
          c_iri := data[0][1];
	}
    }
  return ll;
}
;

create procedure b3s_uri_local_part (in uri varchar)
{
  declare delim integer;
  declare uriSearch varchar;
  delim := -1;
  uriSearch := uri;
  delim := coalesce (strrchr (uriSearch, '/'), 0);
  delim := __max (delim, coalesce (strrchr (uriSearch, '#'), 0));
  delim := __max (delim, coalesce (strrchr (uriSearch, ':'), 0));
  if (delim > 0)
    uriSearch := subseq (uri, delim + 1);
  return uriSearch;
}
;

create procedure
b3s_render_fct_link ()
{
  declare sid varchar;
  sid := connection_get ('sid');

  if (sid is not null)
    return ('/fct/facet.vsp?sid='||sid||'&cmd=refresh');  
  else
    return '';
}
;

create procedure
b3s_render_inf_opts () 
{
  declare inf varchar;
  declare f int;
  f := 0;
  inf := connection_get ('inf');

  for select distinct RS_NAME as RS_NAME from SYS_RDF_SCHEMA do 
    {
      if (RS_NAME = inf) 
        {
          http (sprintf ('<option value="%s" selected="selected">%s</option>', RS_NAME, RS_NAME));
          f := 1;
        }
      else 
        http (sprintf ('<option value="%s">%s</option>', RS_NAME, RS_NAME));
    }

  if (f = 0)
    http ('<option value="**none**" selected="selected">None</option>');
  else 
    http ('<option value="**none**">None</option>');
}
;

create procedure
b3s_sas_selected ()
{
  if (connection_get ('sas') = 'yes') 
    return ' checked="true" ';
  else 
    return ''; 
}
;
 
create procedure 
b3s_parse_inf (in sid varchar, inout params any)
{
  declare _sas, _inf varchar;

  _sas := _inf := null; 

  if (sid is not null)
    { 
      for select fct_state from fct_state where fct_sid = sid do
        {
	  declare i varchar;
          connection_set('inf', fct_inf_val (fct_state));
          connection_set('sas', fct_sas_val (fct_state));
	  i := cast (xpath_eval ('/query/@s-term', fct_state) as varchar);
	  if (length (i))
            connection_set('s_term', i);
        }
    }

-- URL params override

  _inf := get_keyword ('inf', params);

  if (_inf is not null)
    {
      if (exists (select 1 from SYS_RDF_SCHEMA where rs_name = _inf))
        connection_set ('inf', _inf);
      else connection_set ('inf', null);
    }

  _sas := get_keyword ('sas', params);

  if (_sas is not null)
    {
      if (_sas = '1' or _sas = 'yes')
        connection_set ('sas', 'yes');
      else 
        connection_set ('sas', null);
    }
}
;

create procedure
b3s_render_inf_clause ()
{
  declare _inf, _sas varchar;

  _inf := connection_get ('inf');
  _sas := connection_get ('sas');

  if (_inf is not null) 
    _inf := sprintf (' define input:inference ''%s'' ', _inf);
  else 
    _inf := '';

  if (_sas is not null)
    _sas := sprintf (' define input:same-as "yes" ');
  else 
    _sas := '';

  return (_inf || _sas); 
}
;

create procedure
b3s_render_ses_params () 
{
  declare i,s,ifp,sid varchar;

  i := connection_get ('inf');
  s := connection_get ('sas');
  sid := connection_get ('sid');

  if (i is not null) i := '&inf=' || sprintf ('%U', i);
  if (s is not null) i := i || '&sas=' || sprintf ('%V', s);
  if (sid is not null) i := i || '&sid=' || sprintf ('%V', sid);

  if (i is not null) return i;
  else return '';
}
;

create procedure 
b3s_dbg_out (inout ses any, in str any)
{
  if (connection_get ('b3s_dbg'))
    http (str || '\n', ses);
}
;

create procedure 
b3s_render_dbg_out (inout ses any)
{
  if (connection_get ('b3s_dbg')) 
    {
      http('<div id="dbg_output"><pre>');
      http_value (ses);
      http('</pre></div>');
    }
}
;

create procedure b3s_get_lang_by_q (in accept varchar, in lang varchar)
{
  declare format, itm, q varchar;
  declare arr any;
  declare i, l int;

  arr := split_and_decode (accept, 0, '\0\0,;');
  q := 0;
  l := length (arr);
  format := null;
  for (i := 0; i < l; i := i + 2)
    {
      declare tmp any;
      itm := trim(arr[i]);
      if (itm = lang)
	{
	  q := arr[i+1];
	  if (q is null)
	    q := 1.0;
	  else
	    {
	      tmp := split_and_decode (q, 0, '\0\0=');
	      if (length (tmp) = 2)
		q := atof (tmp[1]);
	      else
		q := 1.0;
	    }
	  goto ret;
	}
    }
  ret:
  if (q = 0 and lang = 'en')
    q := 0.002;
  if (q = 0 and not length (lang))
    q := 0.001;
  return q;
}
;

create procedure b3s_label_get (inout data any, in langs any)
{
  declare q, best_q, label any;
  label := '';
   if (length (data))
     {
       best_q := 0;
       for (declare i,l int, i := 0, l := length (data); i < l; i := i + 1)
         {
	  q := b3s_get_lang_by_q (langs, data[i][1]);
	  --dbg_obj_print (data[i][0], langs, data[i][1], q);
          if (q > best_q)
	    {
	      label := data[i][0];
	      best_q := q;
	    }
	 }
     }
   if (__tag of rdf_box = __tag (label))
     label := rdf_box_data (label);
   if (not isstring (label))
     label := cast (label as varchar);
   --label := regexp_replace (label, '<[^>]+>', '', 1, null);  
  if (0 and sys_stat ('cl_run_local_only'))
    {
   label := xpath_eval ('string(.)', xtree_doc (label, 2));
   label := charset_recode (label, '_WIDE_', 'UTF-8');
    }
  else
    label := cast (xtree_doc (label, 2) as varchar);
   return label;
}
;

create procedure 
b3s_rel_print (in val any, in rel any, in flag int := 0)
{
  declare delim, delim1, delim2, delim3 integer;
  declare inx int;
  declare nss, loc, nspref varchar;

  delim1 := coalesce (strrchr (val, '/'), -1);
  delim2 := coalesce (strrchr (val, '#'), -1);
  delim3 := coalesce (strrchr (val, ':'), -1);
  delim := __max (delim1, delim2, delim3);
  nss := null;
  loc := val;
  if (delim < 0) return loc;
  nss := subseq (val, 0, delim + 1);
  loc := subseq (val, delim + 1);

  nspref := __xml_get_ns_prefix (nss, 2);
  if (nspref is null)
    {
      inx := connection_get ('ns_ctr');
      connection_set ('ns_ctr', inx + 1);
      nspref := sprintf ('ns%d', inx);
    }


  nss := sprintf ('xmlns:%s="%s"', nspref, nss);
  if (flag)
    loc := sprintf ('property="%s:%s"', nspref, loc);
  else if (rel)
    loc := sprintf ('rel="%s:%s"', nspref, loc);
  else
    loc := sprintf ('rev="%s:%s"', nspref, loc);
  return concat (loc, ' ', nss);
}
;


create procedure 
b3s_uri_curie (in uri varchar)
{
  declare delim integer;
  declare uriSearch, nsPrefix varchar;

  delim := -1;

  uriSearch := uri;
  if (uri is null)
    return '';
  nsPrefix := null;
  while (nsPrefix is null and delim <> 0) {

    delim := coalesce (strrchr (uriSearch, '/'), 0);
    delim := __max (delim, coalesce (strrchr (uriSearch, '#'), 0));
    delim := __max (delim, coalesce (strrchr (uriSearch, ':'), 0));

    nsPrefix := coalesce(__xml_get_ns_prefix(subseq(uriSearch, 0, delim + 1),2), __xml_get_ns_prefix(subseq(uriSearch, 0, delim),2));
    uriSearch := subseq(uriSearch, 0, delim);
--    dbg_obj_print(uriSearch);
  }

  if (nsPrefix is not null) {
	declare rhs varchar;
	rhs := subseq(uri, length(uriSearch) + 1, null);
	if (length(rhs) = 0) {
		return uri;
	} else {
		return nsPrefix || ':' || rhs;
	}
  }
  return uri;
}
;

create procedure b3s_prop_label (in uri any)
{
  declare ll varchar;
  ll := (select top 1 __ro2sq (O) from DB.DBA.RDF_QUAD where S = __i2idn (uri) and P = __i2idn ('http://www.w3.org/2000/01/rdf-schema#label') OPTION (QUIETCAST));
  if (length (ll) = 0)
    ll := b3s_uri_curie (uri);
  if (isstring (ll) and ll like 'opl%:isDescribedUsing')
    ll := 'Described Using Terms From';  
  return ll;
}
;

create procedure
b3s_trunc_uri (in s varchar, in maxlen int := 80)
{
  declare _s varchar;
  declare _h int; 

  _s := trim(s);

  if (length(_s) <= maxlen) return _s;
  _h := floor ((maxlen-3) / 2);
  _s := sprintf ('%s...%s', "LEFT"(_s, _h), "RIGHT"(_s, _h-1));

  return _s;
}
;

create procedure 
b3s_http_url (in url varchar, in sid varchar := null, in _from varchar := null)
{
  declare host, pref, more, i varchar;

--  more := '';

--  if (sid is not null)
--    more := sprintf ('&sid=%s', sid);
--  else
--    more := '';

  i := b3s_render_ses_params();
  if (length (_from))
    i := sprintf ('%s&graph=%U', i, _from);
  
  return sprintf ('/describe/?url=%U%s', url, i);
};

create procedure 
b3s_http_print_l (in p_text any, inout odd_position int, in r int := 0, in sid varchar := null)
{
   declare short_p, p_prefix, int_redirect, url any;

   odd_position :=  odd_position + 1;
   p_prefix := b3s_prop_label (p_text);
   url := b3s_http_url (p_text, sid);

   if (not length (p_text))
     return;

   http (sprintf ('<tr class="%s"><td class="property">', either(mod (odd_position, 2), 'odd', 'even')));

   if (r) http ('is ');

   http (sprintf ('<a class="uri" href="%s" title="%s">%s</a>\n', 
                  url, 
                  p_prefix, 
                  b3s_trunc_uri (p_prefix, 40)));

   if (r) http (' of');

   http ('</td><td><ul class="obj">');
}
;

create procedure b3s_label (in _S any, in langs any)
{
  declare best_str, meta, data any;
  declare best_q, q float;
  declare lang, stat, msg varchar;

  stat := '00000';
  exec (sprintf ('sparql define input:inference "virtrdf-label" '||
  'select ?o (lang(?o)) where { <%S> virtrdf:label ?o }', _S), stat, msg, vector (), 0, meta, data);
  if (stat <> '00000')
    return '';
  best_str := '';
  best_q := 0;
  if (length (data))
    {
      for (declare i, l int, i := 0, l := length (data); i < l; i := i + 1)
	{
	  q := rdfdesc_get_lang_by_q (langs, data[i][1]);
	  --dbg_obj_print (data[i][0], langs, data[i][1], q);
          if (q > best_q)
	    {
	      best_str := data[i][0];
	      best_q := q;
	    }
	}
    }
  if (__tag (best_str) = 246)
    best_str := rdf_box_data (best_str);
  return best_str;
}
;

create procedure 
b3s_http_print_r (in _object any, in sid varchar, in prop any, in langs any, in rel int := 1, in acc any := null, in _from varchar := null)
{
   declare lang, rdfs_type, rdfa, visible any;

   if (_object is null) 
     return;

   if (__tag (_object) = 230)
     {
       rdfs_type := 'XMLLiteral';
       lang := '';
     }
   else
     {
       declare exit handler for sqlstate '*' {
         lang := '';
	 rdfs_type := 'http://www.w3.org/2001/XMLSchema#string';
	 goto endg;
       };
       lang := DB.DBA.RDF_LANGUAGE_OF_OBJ (_object);
       rdfs_type := DB.DBA.RDF_DATATYPE_OF_OBJ (_object);
       endg:;
     }
   if (__tag of IRI_ID = __tag (rdfs_type))
     rdfs_type := id_to_iri (rdfs_type);

   rdfa := b3s_rel_print (prop, rel, 1);
   visible := b3s_str_lang_check (lang, acc);
   http (sprintf ('\t<li%s><span class="literal">', case visible when 0 then ' style="display:none;"' else '' end));
again:
   if (__tag (_object) = 246)
     {
       declare dat any;
       dat := __rdf_sqlval_of_obj (_object, 1);
       _object := dat;
       goto again;
     }
   else if (__tag (_object) = 243 or (isstring (_object) and (__box_flags (_object)= 1 or _object like 'nodeID://%' or _object like 'http://%')))
     {
       declare _url, p_t any;

       if (__tag of IRI_ID = __tag (_object))
	 _url := id_to_iri (_object);
       else
	 _url := _object;

       if (not length (_url))
         return;	 

       http (sprintf ('<!-- %d -->', length (_url)));

       rdfa := b3s_rel_print (prop, rel, 0);
       if (http_mime_type (_url) like 'image/%')
	 http (sprintf ('<a class="uri" %s href="%s"><img src="%s" height="160" style="border-width:0" alt="External Image" /></a>', rdfa, b3s_http_url (_url, sid, _from), _url));
       else
	 {
	   declare lbl any;
	   lbl := '';
	   if (registry_get ('fct_desc_value_labels') = '1' and (__tag (_object) = 243 or (isstring (_object) and __box_flags (_object) = 1)))
	     lbl := b3s_label (_url, langs);
	   if ((not isstring(lbl)) or length (lbl) = 0)
	     lbl := b3s_uri_curie(_url);
	   -- XXX: must encode as wide label to print correctly  
	   --http (sprintf ('<a class="uri" %s href="%s">%V</a>', rdfa, b3s_http_url (_url, sid, _from), lbl));
	   http (sprintf ('<a class="uri" %s href="%s">', rdfa, b3s_http_url (_url, sid, _from)));
	   http_value (charset_recode (lbl, 'UTF-8', '_WIDE_'));
	   http (sprintf ('</a>'));
	 }
       --if (registry_get ('fct_sponge') = '1' and _url like 'http://%' or _url like 'https://%')
       --	 http (sprintf ('&nbsp;<a class="uri" href="%s&sp=1"><img src="/fct/images/goout.gif" title="Sponge" border="0"/></a>', 
       --	       b3s_http_url (_url, sid)));

     }
   else if (__tag (_object) = 189)
     {
       http (sprintf ('<span %s>%d</span>', rdfa, _object));
       lang := 'xsd:integer';
     }
   else if (__tag (_object) = 190)
     {
       http (sprintf ('<span %s>%f</span>', rdfa, _object));
       lang := 'xsd:float';
     }
   else if (__tag (_object) = 191)
     {
       http (sprintf ('<span %s>%d</span>', rdfa, _object));
       lang := 'xsd:double';
     }
   else if (__tag (_object) = 219)
     {
       http (sprintf ('<span %s>%s</span>', rdfa, cast (_object as varchar)));
       lang := 'xsd:double';
     }
   else if (__tag (_object) = 182)
     {
       http (sprintf ('<span %s>', rdfa));
       http (_object);
       http ('</span>');
       lang := '';
     }
   else if (__tag (_object) = 211)
     {
       http (sprintf ('<span %s>%s</span>', rdfa, datestring (_object)));
       lang := 'xsd:date';
     }
   else if (__tag (_object) = 230)
     {
       _object := serialize_to_UTF8_xml (_object);
       _object := replace (_object, '<xhtml:', '<');
       _object := replace (_object, '</xhtml:', '</');
       http (sprintf ('<span %s>', rdfa));
       http (_object);
       http ('</span>');
       http (sprintf ('(%s)', rdfs_type));
     }
   else if (__tag (_object) = 225)
     {
       http (sprintf ('<span %s>', rdfa));
     http (charset_recode (_object, '_WIDE_', 'UTF-8'));
       http ('</span>');
     }
   else if (__tag (_object) = 238)
     {
       http (sprintf ('<span %s>', rdfa));
       http (cast (_object as varchar));
       http ('</span>');
     }
   else if (__tag (_object) = 222)
     {
       http (sprintf ('<span %s>', rdfa));
       http (cast (_object as varchar));
       http ('</span>');
     }
   else if (__tag (_object) = 126)
     {
       http (sprintf ('<span %s>', rdfa));
       http ('&lt;binary object&gt;');
       http ('</span>');
     }
   else
     http (sprintf ('FIXME %i', __tag (_object)));

   if (lang is not NULL and lang <> '')
     {
       http (sprintf ('(%s)', lang));
     }

   http ('</span></li>');
   return visible;
}
;

create procedure b3s_page_get_short (in val any)
{
  declare ret, pos any;
  declare delim1, delim2, delim3 integer;

  ret := split_and_decode (val, 0, '\0\0/');
  ret := ret[length(ret)-1];

  delim1 := coalesce (strrchr (val, '/'), -1);
  delim2 := coalesce (strrchr (val, '#'), -1);
  delim3 := coalesce (strrchr (val, ':'), -1);
  pos := __max (delim1, delim2, delim3);

  if (pos is not NULL)
    {
      ret := subseq (val, pos + 1);
    }

   return ret;
}
;

create procedure fct_links_hdr (in subj any, in desc_link any)
{
  declare links varchar;
  desc_link := sprintf ('http://%{WSHost}s%s', desc_link);
  links := 'Link: ';
  links := links || 
  sprintf ('<%s&output=application%%2Frdf%%2Bxml>; rel="alternate"; type="application/rdf+xml"; title="Structured Descriptor Document (RDF/XML format)",', desc_link);
  links := links || 
  sprintf ('<%s&output=text%%2Fn3>; rel="alternate"; type="text/n3"; title="Structured Descriptor Document (N3/Turtle format)",', desc_link);
  links := links || 
  sprintf ('<%s&output=application%%2Frdf%%2Bjson>; rel="alternate"; type="application/rdf+json"; title="Structured Descriptor Document (RDF/JSON format)",', desc_link);
  links := links || 
  sprintf ('<%s&output=application%%2Fatom%%2Bxml>; rel="alternate"; type="application/atom+xml"; title="Structured Descriptor Document (OData/Atom format)",', desc_link);
  links := links || 
  sprintf ('<%s&output=application%%2Fodata%%2Bjson>; rel="alternate"; type="application/odata+json"; title="Structured Descriptor Document (OData/JSON format)",', desc_link);
  links := links || 
  sprintf ('<%s&output=text%%2Fcxml>; rel="alternate"; type="text/cxml"; title="Structured Descriptor Document (CXML format)",', desc_link);
  links := links || 
  sprintf ('<%s&output=text%%2Fcsv>; rel="alternate"; type="text/csv"; title="Structured Descriptor Document (CSV format)",', desc_link);
  links := links || sprintf ('<%s>; rel="http://xmlns.com/foaf/0.1/primaryTopic",', subj);
  links := links || sprintf ('<%s>; rev="describedby"\r\n', subj);
  http_header (http_header_get () || links);
}
;


create procedure fct_links_mup (in subj any, in desc_link any)
{
  declare links varchar;
  desc_link := sprintf ('http://%{WSHost}s%s', desc_link);
  links := '';
  links := links || repeat (' ', 5) ||
  sprintf ('<link href="%V&amp;output=application%%2Frdf%%2Bxml" rel="alternate" type="application/rdf+xml"  title="Structured Descriptor Document (RDF/XML format)" />\n', desc_link);
  links := links || repeat (' ', 5) ||
  sprintf ('<link href="%V&amp;output=text%%2Fn3" rel="alternate" type="text/n3" title="Structured Descriptor Document (N3/Turtle format)" />\n', desc_link);
  links := links || repeat (' ', 5) ||
  sprintf ('<link href="%V&amp;output=application%%2Frdf%%2Bjson" rel="alternate" type="application/rdf+json" title="Structured Descriptor Document (RDF/JSON format)" />\n', desc_link);
  links := links || repeat (' ', 5) ||
  sprintf ('<link href="%V&amp;output=application%%2Fatom%%2Bxml" rel="alternate" type="application/atom+xml" title="Structured Descriptor Document (OData/Atom format)" />\n', desc_link);
  links := links || repeat (' ', 5) ||
  sprintf ('<link href="%V&amp;output=application%%2Fatom%%2Bjson" rel="alternate" type="application/atom+json" title="Structured Descriptor Document (OData/JSON format)" />\n', desc_link);
  links := links || repeat (' ', 5) ||
  sprintf ('<link href="%V&amp;output=text%%2Fcxml" rel="alternate" type="text/cxml" title="Structured Descriptor Document (CXML format)" />\n', desc_link);
  links := links || repeat (' ', 5) ||
  sprintf ('<link href="%V&amp;output=text%%2Fcsv" rel="alternate" type="text/csv" title="Structured Descriptor Document (CSV format)" />\n', desc_link);
  links := links || repeat (' ', 5) || sprintf ('<link href="%V" rel="http://xmlns.com/foaf/0.1/primaryTopic" />\n', subj);
  links := links || repeat (' ', 5) || sprintf ('<link href="%V" rev="describedby" />\n', subj);
  http (links);
}
;

create procedure
fct_make_selector (in subj any, in sid integer) 
{
  return null;
}	
;

create procedure fct_make_qr_code (in data_to_qrcode any, in src_width int := 120, in src_height int := 120, in qr_scale int := 4)
{
  declare qrcode_bytes, mixed_content, content varchar;
  declare qrcode any;

  if (__proc_exists ('QRcode encodeString8bit', 2) is null)
    return null;

  declare exit handler for sqlstate '*' { return null; };

  content := "IM CreateImageBlob" (src_width, src_height, 'white', 'jpg');
  qrcode := "QRcode encodeString8bit" (data_to_qrcode);
  qrcode_bytes := aref_set_0 (qrcode, 0);
  mixed_content := "IM PasteQRcode" (qrcode_bytes, qrcode[1], qrcode[2], qr_scale, qr_scale, 0, 0, cast (content as varchar), length (content));
  mixed_content := encode_base64 (cast (mixed_content as varchar));
  mixed_content := replace (mixed_content, '\r\n', '');
  return mixed_content;
}
;

create procedure fct_make_curie (in url varchar, in lines any)
{
  declare curie, chost, dhost varchar;
  if (__proc_exists ('WS.CURI.curi_make_curi') is null)
    return url;
  curie := WS.CURI.curi_make_curi (url);
  dhost := registry_get ('URIQADefaultHost');
  chost := http_request_header(lines, 'Host', null, dhost);
  return sprintf ('http://%s/c/%s', chost, curie);
}
;


create procedure DB.DBA.SPARQL_DESC_DICT_LOD_PHYSICAL (in subj_dict any, in consts any, in good_graphs any, in bad_graphs any, in storage_name any, in options any)
{
  declare all_subj_descs, phys_subjects, sorted_good_graphs, sorted_bad_graphs, g_dict, res any;
  declare uid, graphs_listed, g_ctr, good_g_count, bad_g_count, s_ctr, all_s_count, phys_s_count integer;
  declare gs_app_callback, gs_app_uid varchar;

  if (isinteger (consts))
    goto normal;

  uid := get_keyword ('uid', options, http_nobody_uid());
  gs_app_callback := get_keyword ('gs-app-callback', options);
  if (gs_app_callback is not null)
    gs_app_uid := get_keyword ('gs-app-uid', options);

  phys_subjects := dict_list_keys (subj_dict, 0);
  phys_subjects := vector_concat (phys_subjects, consts);
  phys_s_count := length (phys_subjects);
  if (__tag of integer = __tag (good_graphs))
    {
      g_dict := dict_new ();
      vectorbld_init (sorted_bad_graphs);
      foreach (any g in bad_graphs) do
	{
	  if (isiri_id (g) and g < min_bnode_iri_id ())
	    vectorbld_acc (sorted_bad_graphs, g);
	}
      vectorbld_final (sorted_bad_graphs);
      for (s_ctr := phys_s_count - 1; s_ctr >= 0; s_ctr := s_ctr - 1)
	{
	  declare subj, graph any;
	  subj := phys_subjects [s_ctr];
	  graph := coalesce ((select top 1 G as g1 from DB.DBA.RDF_QUAD where O = subj and 0 = position (G, sorted_bad_graphs) and
	    __rgs_ack_cbk (G, uid, 1) and
	    (gs_app_callback is null or bit_and (1, call (gs_app_callback) (G, gs_app_uid))) ) );
	  if (graph is not null)
	    dict_put (g_dict, graph, 0);
	}
      sorted_good_graphs := dict_list_keys (g_dict, 0);
      if (0 = length (sorted_good_graphs))
	{
	  g_dict := dict_new ();
	  for (s_ctr := phys_s_count - 1; s_ctr >= 0; s_ctr := s_ctr - 1)
	    {
	      declare subj, graph any;
	      subj := phys_subjects [s_ctr];
	      graph := coalesce ((select top 1 G as g1 from DB.DBA.RDF_QUAD where S = subj and 0 = position (G, sorted_bad_graphs) and
		__rgs_ack_cbk (G, uid, 1) and
		(gs_app_callback is null or bit_and (1, call (gs_app_callback) (G, gs_app_uid))) ) );
	      if (graph is not null)
		dict_put (g_dict, graph, 0);
	    }
	  sorted_good_graphs := dict_list_keys (g_dict, 1);
	}
      if (0 < length (sorted_good_graphs))
	good_graphs := sorted_good_graphs;
    }
  normal:
  return DB.DBA.SPARQL_DESC_DICT (subj_dict, consts, good_graphs, bad_graphs, storage_name, options);
}
;

create procedure DB.DBA.SPARQL_DESC_DICT_LOD (in subj_dict any, in consts any, in good_graphs any, in bad_graphs any, in storage_name any, in options any)
{
  return DB.DBA.SPARQL_DESC_DICT (subj_dict, consts, good_graphs, bad_graphs, storage_name, options);
}
;

grant execute on DB.DBA.SPARQL_DESC_DICT_LOD_PHYSICAL to "SPARQL_SELECT";
grant execute on DB.DBA.SPARQL_DESC_DICT_LOD to "SPARQL_SELECT";
