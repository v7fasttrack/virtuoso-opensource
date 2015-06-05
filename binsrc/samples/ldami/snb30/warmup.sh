#!/bin/bash

java -Xmx8g -cp /home/ldbc_driver/target/jeeves-0.2-SNAPSHOT.jar:/home/ldbc_snb_implementations/interactive/virtuoso/java/virtuoso/target/virtuoso-0.0.1-SNAPSHOT.jar:/home/ldbc_snb_implementations/interactive/virtuoso/java/virtuoso/virtjdbc4_lib/virtjdbc4.jar com.ldbc.driver.Client -db com.ldbc.driver.workloads.ldbc.snb.interactive.db.VirtuosoDb -P ./ldbc_snb_interactive_SF-0030.properties -P ./ldbc_driver_default.properties -P virtuoso_configuration.properties -P /home/snb30/outputDir/updates/updateStream.properties -oc 100000 -p "ldbc.snb.interactive.LdbcUpdate1AddPerson_enable|false" -p "ldbc.snb.interactive.LdbcUpdate2AddPostLike_enable|false" -p "ldbc.snb.interactive.LdbcUpdate3AddCommentLike_enable|false" -p "ldbc.snb.interactive.LdbcUpdate4AddForum_enable|false" -p "ldbc.snb.interactive.LdbcUpdate5AddForumMembership_enable|false" -p "ldbc.snb.interactive.LdbcUpdate6AddPost_enable|false" -p "ldbc.snb.interactive.LdbcUpdate7AddComment_enable|false" -p "ldbc.snb.interactive.LdbcUpdate8AddFriendship_enable|false"