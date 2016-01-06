<?php

/**
 * Builds a table list of messages for the current user.
 */
function cwrc_dashboards_user_privatemsg_block() {

  // Quick permission check.
  if (!user_access('read privatemsg')) {
    return false;
  }

  global $user;

  // Get the threads.
  $threads = privatemsg_sql_list($user)
    ->limit(5)
    ->execute();

  // No messages to display.
  if ($threads->rowCount() == 0) {
    return t('You have no messages in your inbox.');
  }

  // Build table render array.
  $build = array(
    'table' => array(
      '#theme' => 'table',
      '#rows' => array(),
      '#header' => array(
        t('Subject'),
        t('From'),
        t('Received on'),
      ),
    ),
    'link' => array(
      '#markup' => '<span class="view-all-messages">' . l(t('View all messages'), 'user/' . $user->uid . '/messages') . '</a></span>',
    ),
    'new-link' => array(
      '#markup' => '<span class="compose-new-message">' . l(t('New message'), 'messages/new') . '</a></span>',
    ),
  );

  // Fill rows with messages.
  foreach ($threads as $thread) {
    // Get participants and build display.
    $participants = privatemsg_sql_participants($thread->thread_id, $user)->execute();
    $participant_output = array();

    foreach ($participants as $participant) {
      if ($participant->recipient != $user->uid) {
        $account = user_load($participant->recipient);
        $participant_output[] = l(realname_load($account), 'user/' . $participant->recipient);
      }
    }

    // Build the rows.
    $build['table']['#rows'][] = array(
      'class' => ($thread->is_new) ? array('is-new') : array(),
      'data' => array(
        l(filter_xss(decode_entities($thread->subject), array()), 'messages/view/' . $thread->thread_id),
        implode(', ', $participant_output),
        format_date($thread->last_updated),
      ),
    );
  }

  // Return render array.
  return $build;
}

/**
 * Shows objects that are assigned the current user.
 */
function cwrc_dashboards_user_assigned_to_me_block() {
  global $user;
  global $language;

  // Get solarium client.
  $client = cwrc_dashboards_get_solarium();

  // Build select query for assigned to me block.
  $query = $client->createSelect();
  $query->setQuery('workflow_assigned_message_recipient_userID_current_ms:(' . $user->name . ')')
    ->setFields(array(
      "PID",
      "fgs_label_s",
      "RELS_EXT_lock-expiry_literal_s",
      "RELS_EXT_locked-by_literal_s",
      "ancestors_ms",
      "workflow_userID_current_ms",
      "workflow_activity_category_current_ms",
      "workflow_activity_stamp_current_ms",
      "workflow_date_current_dt",
      "workflow_activity_note_current_ms",
    ))
    ->addSort('workflow_date_current_dt', $query::SORT_DESC);

  // Get query results.
  $results = $client->select($query);

  // Nothing assigned to the current user.
  if ($results->getNumFound() == 0) {
    return t('Nothing assigned to you at the moment.');
  }

  // Build table render array.
  $build = array(
    '#theme' => 'table',
    '#rows' => array(),
    '#header' => array(
      '',
      t('Object'),
      '',
      t('Time assigned'),
      t('Assigned by'),
      t('Stamp'),
      t('Note'),
    ),
  );

  // Fill rows with objects.
  foreach ($results as $document) {
    // Determine lock status.
    // TODO: This is ugly, make a helper.
    if (isset($document['RELS_EXT_lock-expiry_literal_s'])) {
      $lock_expiry = new DateTime($document['RELS_EXT_lock-expiry_literal_s']);
      $now = new DateTime();
      if ($lock_expiry > $now) {
        if ($document['RELS_EXT_locked-by_literal_s'] == $user->name) {
          // Locked and owned by current user
          $locked = 'cwrc-object-locked-owned';
          $locked_message = 'Locked (you own the lock)';
        } else {
          $locked = 'cwrc-object-locked-not-owned';
          $locked_message = 'Locked';
        }
      } else {
        $locked = 'cwrc-object-not-locked';
        $locked_message = 'Not locked';
      }
    } else {
      $locked = 'cwrc-object-not-locked';
      $locked_message = 'Not locked';
    }

    // Determine parent project.
    $project = _cwrc_projects_get_project($document['PID'], $document['ancestors_ms']);
    if ($project !== false) {
      $project_uri = entity_uri('node', $project);
      $wrapper = entity_metadata_wrapper('node', $project);
      $wrapper->language($language->language);

      // Get the logo, hide project if no logo is present.
      $logo = $wrapper->field_logo->value();
      if ($logo !== null) {
        $logo = theme_image_style(array(
          'style_name' => 'icon',
          'path' => $logo['uri'],
          'width' => $logo['metadata']['width'],
          'height' => $logo['metadata']['height'],
          'alt' => (empty($logo['alt'])) ? $wrapper->title_field->value() : $logo['alt'],
        ));
      } else {
        $project = false;
      }
    }

    // Get the user who assigned this work.
    $assigned_by = user_load_by_name($document['workflow_userID_current_ms'][0]);

    // Build the row.
    $build['#rows'][] = array(
      // Add a class for workflow stamp styling.
      'class' => array('cwrc-workflow-' . $document['workflow_activity_category_current_ms'][0]),

      // Build the columns.
      'data' => array(
        // Project logo (if available).
        ($project !== false) ? l($logo, $project_uri['path'], array('html' => true)) : '',

        // Object link.
        l($document['fgs_label_s'], 'islandora/object/' . $document['PID']),

        // Lock status.
        array(
          'class' => array($locked, 'object-locked-column'),
          'data' => array(
            array(
              '#type' => 'html_tag',
              '#tag' => 'span',
              '#attributes' => array('class' => $locked),
              '#value' => '',
            ),
            array(
              '#type' => 'html_tag',
              '#tag' => 'span',
              '#attributes' => array('class' =>'hide-txt'),
              '#value' => $locked_message,
            ),
          ),
        ),

        // Time assigned, assigned by and stamp.
        (isset($document['workflow_date_current_dt']) ? format_date(strtotime($document['workflow_date_current_dt'])) : ''),
        l(realname_load($assigned_by), 'user/' . $assigned_by->uid),
        $document['workflow_activity_stamp_current_ms'][0],
        $document['workflow_activity_note_current_ms'][0],
      ),
    );
  }

  // Return render array.
  return $build;
}

/**
 * Function that builds a list of objects this user has edited recently.
 *
 * This is dependant on SOLR and the workflow datastream being indexed properly,
 * see https://github.com/cwrc/basic-solr-config for required SOLR config.
 */
function cwrc_dashboards_user_recently_edited_block() {
  global $user;
  global $language;

  // Get solarium client.
  $client = cwrc_dashboards_get_solarium();

  // Build select query for assigned to me block.
  $query = $client->createSelect();
  $query->setQuery('workflow_userID_current_ms:(' . $user->name . ')')
    ->setFields(array(
      "PID",
      "fgs_label_s",
      "RELS_EXT_lock-expiry_literal_s",
      "RELS_EXT_locked-by_literal_s",
      "ancestors_ms",
      "workflow_activity_category_current_ms",
      "workflow_date_current_dt",
      "workflow_activity_note_current_ms",
      "workflow_activity_status_current_ms",
    ))
    ->addSort('workflow_date_current_dt', $query::SORT_DESC)
    ->setRows(5);

  // Get query results.
  $results = $client->select($query);

  // No recently edited objects for the current user.
  if ($results->getNumFound() == 0) {
    return t('No recently edited objects found.');
  }

  // Build table render array.
  $build = array(
    '#theme' => 'table',
    '#rows' => array(),
    '#header' => array(
      '',
      t('Object'),
      '',
      t('Time changed'),
      t('Category'),
      t('Status'),
      t('Note'),
    ),
  );

  // Fill rows with objects.
  foreach ($results as $document) {
    // Determine lock status.
    // TODO: This is ugly, make a helper.
    if (isset($document['RELS_EXT_lock-expiry_literal_s'])) {
      $lock_expiry = new DateTime($document['RELS_EXT_lock-expiry_literal_s']);
      $now = new DateTime();
      if ($lock_expiry > $now) {
        if ($document['RELS_EXT_locked-by_literal_s'] == $user->name) {
          // Locked and owned by current user
          $locked = 'cwrc-object-locked-owned';
          $locked_message = 'Locked (you own the lock)';
        } else {
          $locked = 'cwrc-object-locked-not-owned';
          $locked_message = 'Locked';
        }
      } else {
        $locked = 'cwrc-object-not-locked';
        $locked_message = 'Not locked';
      }
    } else {
      $locked = 'cwrc-object-not-locked';
      $locked_message = 'Not locked';
    }

    // Determine parent project.
    $project = _cwrc_projects_get_project($document['PID'], $document['ancestors_ms']);
    if ($project !== false) {
      $project_uri = entity_uri('node', $project);
      $wrapper = entity_metadata_wrapper('node', $project);
      $wrapper->language($language->language);

      // Get the logo, hide project if no logo is present.
      $logo = $wrapper->field_logo->value();
      if ($logo !== null) {
        $logo = theme_image_style(array(
          'style_name' => 'icon',
          'path' => $logo['uri'],
          'width' => $logo['metadata']['width'],
          'height' => $logo['metadata']['height'],
          'alt' => (empty($logo['alt'])) ? $wrapper->title_field->value() : $logo['alt'],
        ));
      } else {
        $project = false;
      }
    }

    // Build the row.
    $build['#rows'][] = array(
      // Add a class for workflow stamp styling.
      'class' => array('cwrc-workflow-' . $document['workflow_activity_category_current_ms'][0]),

      // Build the columns.
      'data' => array(
        // Project logo (if available).
        ($project !== false) ? l($logo, $project_uri['path'], array('html' => true)) : '',

        // Object link.
        l($document['fgs_label_s'], 'islandora/object/' . $document['PID']),

        // Lock status.
        array(
          'class' => array($locked, 'object-locked-column'),
          'data' => array(
            '0' => array(
              '#type' => 'html_tag',
              '#tag' => 'span',
              '#attributes' => array('class' => $locked),
              '#value' => '',
            ),
            '1' => array(
              '#type' => 'html_tag',
              '#tag' => 'span',
              '#attributes' => array('class' =>'hide-txt'),
              '#value' => $locked_message,
            ),
          ),
        ),

        // Assigned, project, time changed, stamp and category
        (isset($document['workflow_date_current_dt']) ? format_date(strtotime($document['workflow_date_current_dt'])) : ''),
        $document['workflow_activity_category_current_ms'][0],
        $document['workflow_activity_status_current_ms'][0],
        $document['workflow_activity_note_current_ms'][0],
      ),
    );
  }

  // Return render array.
  return $build;
}

/**
 * Function that builds a block to display bookmark lists the user owns and
 * bookmark lists shared with the user.
 */
function cwrc_dashboards_user_bookmark_lists_block() {
  global $user;

  // Get bookmark lists owned by the current user.
  $query = db_select('islandora_bookmark_list_names', 'ib')
    ->condition('ib.listowner', $user->uid)
    ->fields('ib');
  $query->leftJoin('islandora_bookmark_list_pids', 'ibp', 'ib.listid = ibp.listid');
  $query->groupBy('ib.listid');
  $query->addExpression('COUNT(ibp.pidid)', 'num_items');
  $query->orderBy('ib.listname');
  $owned = $query->execute()->fetchAllAssoc('listid', PDO::FETCH_ASSOC);

  // Get bookmark lists shared with the current user (exclude owned lists).
  $query = db_select('islandora_bookmark_list_users', 'ibu')
    ->condition('ibu.uid', array(0, $user->uid), 'IN');
  if (count($owned) > 0) {
    $query->condition('ibu.listid', array_keys($owned), 'NOT IN');
  }
  $query->join('islandora_bookmark_list_names', 'ib', 'ibu.listid = ib.listid');
  $query->fields('ib');
  $query->leftJoin('islandora_bookmark_list_pids', 'ibp', 'ib.listid = ibp.listid');
  $query->groupBy('ib.listid');
  $query->addExpression('COUNT(ibp.pidid)', 'num_items');
  $shared = $query->execute()->fetchAllAssoc('listid', PDO::FETCH_ASSOC);

  // Pass to template for easier manipulation.
  return array(
    '#theme' => 'cwrc_dashboards_user_bookmark_lists_block',
    '#owned' => $owned,
    '#shared' => $shared,
  );
}

/**
 * Function that builds a block to display workflow reports the user has access
 * to.
 */
function cwrc_dashboards_user_workflow_reports_block() {
  global $user;

  // Get all workflow reports.
  module_load_include('inc', 'islandora_workflow_rest', 'includes/report_utilities');
  $reports = islandora_workflow_rest_get_all_records();

  $build = array(
    '#theme' => 'cwrc_dashboards_workflow_reports_block',
    '#reports' => array(),
  );

  // Build items for list.
  foreach ($reports as $report) {
    // Unserialize report variables.
    $report['viewroles'] = unserialize($report['viewroles']);
    $report['editroles'] = unserialize($report['editroles']);

    // Intersect arrays to check access.
    $view = array_intersect($report['viewroles'], $user->roles);
    $edit = array_intersect($report['editroles'], $user->roles);

    // View access is granted.
    if (count($view) > 0 || count($edit) > 0) {
      $item['view'] = l($report['name'], "islandora/search/" . $report['query'], array('query' => array('workflow_report' => $report['lid'], 'display' => 'cwrc_report')));

      // Edit access is granted.
      if (count($edit) > 0) {
        $item['edit'] = url("islandora_workflow_rest/reports", array('query' => array('lid' => $report['lid'], 'action' => 'edit')));
        $item['delete'] = url("islandora_workflow_rest/delete_report/{$report['lid']}/{$report['name']}");
      }

      $build['#reports'][] = $item;
    }
  }

  if (user_access(ISLANDORA_WORKFLOW_REST_ISLANDORA_ENTITIES_VIEW_REPORTS)) {
    $build['#admin_access'] = true;
  }

  // Return render array.
  return $build;
}

/**
 * Function that builds a list of objects this user has edited recently.
 *
 * This is dependant on SOLR and the workflow datastream being indexed properly,
 * see https://github.com/cwrc/basic-solr-config for required SOLR config.
 */
function cwrc_dashboards_project_recently_edited_block() {
  $node = menu_get_object();

  if ($node === NULL || !og_is_group('node', $node->nid)) {
    // Not a group.
    return NULL;
  }

  $wrapper = entity_metadata_wrapper('node', $node);

  global $user;

  // Get solarium client.
  $client = cwrc_dashboards_get_solarium();

  // Build select query for assigned to me block.
  $query = $client->createSelect();
  $query->setQuery('ancestors_ms:"' . $wrapper->{CWRC_PROJECTS_COLLECTION_PID_FIELD}->value()->id . '"')
    ->setFields(array(
      "PID",
      "fgs_label_s",
      "RELS_EXT_lock-expiry_literal_s",
      "RELS_EXT_locked-by_literal_s",
      "ancestors_ms",
      "workflow_activity_category_current_ms",
      "workflow_date_current_dt",
      "workflow_activity_status_current_ms",
      "workflow_activity_stamp_current_ms",
      "workflow_userID_current_ms",
    ))
    ->addSort('workflow_date_current_dt', $query::SORT_DESC)
    ->setRows(5);

  // Get query results.
  $results = $client->select($query);

  // No recently edited objects for the current project.
  if ($results->getNumFound() == 0) {
    return t('No recently edited objects found.');
  }

  // Build table render array.
  $build = array(
    '#theme' => 'table',
    '#rows' => array(),
    '#header' => array(
      t('Object'),
      '',
      t('User'),
      t('Time changed'),
      t('Status'),
      t('Category'),
    ),
  );

  // Fill rows with objects.
  foreach ($results as $document) {
    // Determine lock status.
    // TODO: This is ugly, make a helper.
    if (isset($document['RELS_EXT_lock-expiry_literal_s'])) {
      $lock_expiry = new DateTime($document['RELS_EXT_lock-expiry_literal_s']);
      $now = new DateTime();
      if ($lock_expiry > $now) {
        if ($document['RELS_EXT_locked-by_literal_s'] == $user->name) {
          // Locked and owned by current user
          $locked = 'cwrc-object-locked-owned';
          $locked_message = 'Locked (you own the lock)';
        } else {
          $locked = 'cwrc-object-locked-not-owned';
          $locked_message = 'Locked';
        }
      } else {
        $locked = 'cwrc-object-not-locked';
        $locked_message = 'Not locked';
      }
    } else {
      $locked = 'cwrc-object-not-locked';
      $locked_message = 'Not locked';
    }

    // Determine parent project.
    $project = _cwrc_projects_get_project($document['PID'], $document['ancestors_ms']);
    if ($project !== false) {
      $project_uri = entity_uri('node', $project);
    }

    // Get the user who assigned this work.
    $assigned_by = (isset($document['workflow_userID_current_ms'][0])) ? user_load_by_name($document['workflow_userID_current_ms'][0]) : false;

    // Build the row.
    $build['#rows'][] = array(
      // Add a class for workflow stamp styling.
      'class' => array('cwrc-workflow-' . $document['workflow_activity_category_current_ms'][0]),

      // Build the columns.
      'data' => array(
        // Object assigned, project, time changed, stamp and category.
        l($document['fgs_label_s'], 'islandora/object/' . $document['PID']),

        // Lock status.
        array(
          'class' => array($locked, 'object-locked-column'),
          'data' => array(
            '0' => array(
              '#type' => 'html_tag',
              '#tag' => 'span',
              '#attributes' => array('class' => $locked),
              '#value' => '',
            ),
            '1' => array(
              '#type' => 'html_tag',
              '#tag' => 'span',
              '#attributes' => array('class' =>'hide-txt'),
              '#value' => $locked_message,
            ),
          ),
        ),
        ($assigned_by) ? l(realname_load($assigned_by), 'user/' . $assigned_by->uid) : t('Unknown'),
        (isset($documet['workflow_date_current_dt']) ? format_date(strtotime($document['workflow_date_current_dt'])) : ''),
        $document['workflow_activity_status_current_ms'][0],
        $document['workflow_activity_category_current_ms'][0],
      ),
    );
  }

  // Return render array.
  return $build;
}

/**
 * General purpose utilities block for project dashboards.
 */
function cwrc_dashboards_project_utilities_block() {
  $node = menu_get_object();

  if ($node === NULL || !og_is_group('node', $node->nid)) {
    // Not a group.
    return NULL;
  }

  // Wrap the node and get the project collection pid.
  $wrapper = entity_metadata_wrapper('node', $node);
  $pid = $wrapper->{CWRC_PROJECTS_COLLECTION_PID_FIELD}->value()->id;
  $nid = $wrapper->getIdentifier();

  // Start the render array for output (will render as a list).
  $build = array(
    '#theme' => 'item_list',
    '#type' => 'ul',
    '#items' => array(),
    '#attributes' => array(
      'class' => array(
        'cwrc-dashboard-utilities-list',
        'list-group',
      ),
    ),
  );

  // Options with query for find and replace link.
  $options = array('query' => array(
    'display' => 'xquery',
    'f' => array(
      'ancestors_ms:"' . Apache_Solr_Service::escape($pid) . '"',
    ),
  ));

  // Find and replace utility link (islandora_xquery).
  $build['#items'][] = array(
    'data' => l(t('Find and Replace'), 'islandora/search', $options),
    'class' => array('cwrc-dashboards-project-utility-block', 'find-replace'),
  );

  // Back-up/Download utility link (islandora_bagit).
  $build['#items'][] = array(
    'data' => l(t('Back-up/Download'), "islandora/object/$pid/manage/bagit", array('query' => array('destination' => $_GET['q']))),
    'class' => array('cwrc-dashboards-project-utility-block', 'back-up'),
  );

  // Group menu edit link (og_menu_single).
  if (og_user_access('node', $nid, 'manage menu')) {
    $build['#items'][] = array(
      'data' => l(t('Edit project menu'), "group/node/$nid/admin/menu"),
      'class' => array('cwrc-dashboards-project-utility-block', 'edit-menu'),
    );
  }

  // Group page addition link (entityreference_prepopulate).
  if (og_user_access('node', $nid, 'create page content')) {
    $build['#items'][] = array(
      'data' => l(t('Add page to project'), "node/add/page", array('query' => array('og_group_ref' => $nid))),
      'class' => array('cwrc-dashboards-project-utility-block', 'add-page'),
    );
  }

  // Group settings edit link (node).
  if (node_access('update', $wrapper->value())) {
    $build['#items'][] = array(
      'data' => l(t('Edit project settings'), "node/$nid/edit", array('query' => array('destination' => current_path()))),
      'class' => array('cwrc-dashboards-project-utility-block', 'edit-settings'),
    );
  }

  // Return the finished render array.
  return $build;
}

/**
 * Function that builds a block to display workflow reports the user has access
 * to.
 */
function cwrc_dashboards_project_workflow_reports_block() {
  global $user;
  $node = menu_get_object();

  if ($node === NULL || !og_is_group('node', $node->nid)) {
    // Not a group.
    return NULL;
  }

  // Get all workflow reports.
  module_load_include('inc', 'islandora_workflow_rest', 'includes/report_utilities');
  $reports = islandora_workflow_rest_get_all_records();

  $build = array(
    '#theme' => 'cwrc_dashboards_workflow_reports_block',
    '#reports' => array(),
  );

  // Build items for list.
  foreach ($reports as $report) {
    // Only include reports for this project.
    if ($report['project'] == $node->title) {
      // Unserialize report variables.
      $report['viewroles'] = unserialize($report['viewroles']);
      $report['editroles'] = unserialize($report['editroles']);

      // Intersect arrays to check access.
      $view = array_intersect($report['viewroles'], $user->roles);
      $edit = array_intersect($report['editroles'], $user->roles);

      // View access is granted.
      if (count($view) > 0 || count($edit) > 0) {
        $item['view'] = l($report['name'], "islandora/search/" . $report['query'], array('query' => array('workflow_report' => $report['lid'], 'display' => 'cwrc_report')));

        // Edit access is granted.
        if (count($edit) > 0) {
          $item['edit'] = url("islandora_workflow_rest/reports", array('query' => array('lid' => $report['lid'], 'action' => 'edit')));
          $item['delete'] = url("islandora_workflow_rest/delete_report/{$report['lid']}/{$report['name']}");
        }

        $build['#reports'][] = $item;
      }
    }
  }

  // Return render array.
  return $build;
}

/**
 * Function that builds a list of objects that are locked within this project.
 *
 * This is dependant on SOLR and the workflow datastream being indexed properly,
 * see https://github.com/cwrc/basic-solr-config for required SOLR config.
 */
function cwrc_dashboards_project_locked_objects_block_form($form, &$form_state) {
  $node = menu_get_object();

  if ($node === NULL || !og_is_group('node', $node->nid)) {
    // Not a group.
    return NULL;
  }

  $wrapper = entity_metadata_wrapper('node', $node);

  // Get solarium client.
  $client = cwrc_dashboards_get_solarium();

  // Build select query for assigned to me block.
  $query = $client->createSelect();
  $query->setQuery('ancestors_ms:"' . $wrapper->{CWRC_PROJECTS_COLLECTION_PID_FIELD}->value()->id . '" AND RELS_EXT_lock-expiry_literal_s:[* TO *]')
    ->setFields(array(
      "PID",
      "fgs_label_s",
      "RELS_EXT_locked-by_literal_s",
      "RELS_EXT_locked-by_literal_s",
      "RELS_EXT_lock-expiry_literal_s",
    ))
    ->addSort('workflow_date_current_dt', $query::SORT_DESC)
    ->setRows(5);

  // Get query results.
  $results = $client->select($query);

  // Build table select form element.
  $form['objects'] = array(
    '#type' => 'tableselect',
    '#options' => array(),
    '#empty' => t('No locked objects in your project.'),
    '#header' => array(
      'object' => t('Object'),
      'locked_by' => t('Locked by'),
      'expiry' => t('Expiry'),
    ),
  );

  // Fill rows with objects.
  foreach ($results as $document) {
    if (isset($document['RELS_EXT_locked-by_literal_s'])) {
      $locked_by = user_load_by_name($document['RELS_EXT_locked-by_literal_s']);

      $form['objects']['#options'][$document['PID']] = array(
        'object' => l($document['fgs_label_s'], 'islandora/object/' . $document['PID']),
        'locked_by' => l(realname_load($locked_by), 'user/' . $locked_by->uid),
        'expiry' => format_date(strtotime($document['RELS_EXT_lock-expiry_literal_s'])),
      );
    }
  }

  // Submit button (only if there are rows).
  if (count($form['objects']['#options']) > 0) {
    $form['submit'] = array(
      '#type' => 'submit',
      '#executes_submit_callback' => true,
      '#value' => t('Release selected locks'),
      '#submit' => array('cwrc_dashboards_project_locked_objects_block_form_submit'),
    );
  }

  // Return render array.
  return $form;
}
